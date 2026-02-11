//
//  lastfm_queue.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_queue.h"
#include "lastfm_client.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <random>

namespace
{
static const GUID GUID_CFG_LASTFM_PENDING_SCROBBLES = {
    0x9b3b2c41, 0x4c2d, 0x4d8f, {0x9a, 0xa1, 0x1e, 0x37, 0x5b, 0x6a, 0x82, 0x19}};

static const GUID GUID_CFG_LASTFM_DRAIN_COOLDOWN_SECS = {
    0xad4ee3df, 0x0091, 0x4bc9, {0x8b, 0x31, 0x68, 0xa8, 0x09, 0x35, 0x2c, 0x46}};

static const GUID GUID_CFG_LASTFM_DRAIN_ENABLED = {
    0xff0d2adc, 0x0e4b, 0x436a, {0x88, 0xa2, 0x44, 0x98, 0x5c, 0x66, 0x83, 0xe5}};

static const GUID GUID_CFG_LASTFM_DAILY_BUDGET = {
    0x98b413ba, 0xfd05, 0x47c2, {0xb6, 0x5a, 0x94, 0xe4, 0xc1, 0x69, 0x81, 0x13}};

static const GUID GUID_CFG_LASTFM_SCROBBLES_TODAY = {
    0x1f309229, 0x43df, 0x44f4, {0xaf, 0x42, 0x68, 0x63, 0xc6, 0xb4, 0x6f, 0x11}};

static const GUID GUID_CFG_LASTFM_DAY_STAMP = {
    0xb9d93960, 0x37ab, 0x4bd5, {0x89, 0xb1, 0x9d, 0xd3, 0x09, 0x73, 0xea, 0xbd}};

// Dispatch at most 10 per run
static constexpr size_t kMaxDispatchBatch = 10;

// Linear backoff: 60s, 120s, 180s… capped
static constexpr int kRetryStepSeconds = 60;
static constexpr int kRetryMaxSeconds = 60 * 60; // 1h cap

static cfg_string cfgLastfmPendingScrobbles(GUID_CFG_LASTFM_PENDING_SCROBBLES, "");

static cfg_int cfgLastfmDrainCooldownSeconds(GUID_CFG_LASTFM_DRAIN_COOLDOWN_SECS,
                                             360 // 6 minutes
);

static cfg_int cfgLastfmDrainEnabled(GUID_CFG_LASTFM_DRAIN_ENABLED,
                                     1 // enabled by default
);

static cfg_int cfgLastfmDailyBudget(GUID_CFG_LASTFM_DAILY_BUDGET,
                                    2600 // safe default
);

static cfg_int cfgLastfmScrobblesToday(GUID_CFG_LASTFM_SCROBBLES_TODAY, 0);

static cfg_int cfgLastfmDayStamp(GUID_CFG_LASTFM_DAY_STAMP, 0);

struct QueuedScrobble
{
    std::uint64_t id = 0;
    std::string artist;
    std::string title;
    std::string album;
    std::string albumArtist;
    double durationSeconds = 0.0;
    double playbackSeconds = 0.0;
    std::time_t startTimestamp = 0;
    bool refreshOnSubmit = false;
    int retryCount = 0;
    std::time_t nextRetryTimestamp = 0;
};

static std::uint64_t fnv1a64Append(std::uint64_t h, const void* data, std::size_t n)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i)
    {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

static std::uint64_t fnv1a64Str(std::uint64_t h, const std::string& s)
{
    return fnv1a64Append(h, s.data(), s.size());
}

static std::uint64_t computeLegacyId(const QueuedScrobble& q)
{
    std::uint64_t h = 1469598103934665603ull;
    h = fnv1a64Str(h, q.artist);
    h = fnv1a64Str(h, q.title);
    h = fnv1a64Str(h, q.album);

    const auto ts = static_cast<std::int64_t>(q.startTimestamp);
    h = fnv1a64Append(h, &ts, sizeof(ts));

    const auto dur = q.durationSeconds;
    h = fnv1a64Append(h, &dur, sizeof(dur));

    const auto pb = q.playbackSeconds;
    h = fnv1a64Append(h, &pb, sizeof(pb));

    const auto ro = static_cast<std::int32_t>(q.retryCount);
    h = fnv1a64Append(h, &ro, sizeof(ro));

    const auto nr = static_cast<std::int64_t>(q.nextRetryTimestamp);
    h = fnv1a64Append(h, &nr, sizeof(nr));

    const auto rf = static_cast<std::uint8_t>(q.refreshOnSubmit ? 1 : 0);
    h = fnv1a64Append(h, &rf, sizeof(rf));

    if (h == 0)
        h = 1;
    return h;
}

static std::uint64_t nextQueueId()
{
    static std::uint64_t base = []() -> std::uint64_t
    {
        std::random_device rd;
        const std::uint64_t hi = static_cast<std::uint64_t>(rd());
        const std::uint64_t lo = static_cast<std::uint64_t>(rd());
        std::uint64_t v = (hi << 32) ^ lo;
        if (v == 0)
            v = 0x9e3779b97f4a7c15ull; // non-zero fallback
        return v;
    }();

    static std::atomic<std::uint64_t> seq{0};
    const std::uint64_t s = seq.fetch_add(1) + 1;
    const std::uint64_t id = base ^ (s * 0x9e3779b97f4a7c15ull);
    return id ? id : 1;
}

static std::string escapeField(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    for (char c : in)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static std::string unescapeField(const std::string& in)
{
    std::string out;
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '\\' && i + 1 < in.size())
        {
            char n = in[i + 1];
            switch (n)
            {
            case '\\':
                out += '\\';
                ++i;
                continue;
            case 't':
                out += '\t';
                ++i;
                continue;
            case 'n':
                out += '\n';
                ++i;
                continue;
            case 'r':
                out += '\r';
                ++i;
                continue;
            default:
                break;
            }
        }
        out += in[i];
    }
    return out;
}

static std::string serializeScrobble(const QueuedScrobble& q)
{
    std::string out;
    out += escapeField(q.artist);
    out += '\t';
    out += escapeField(q.title);
    out += '\t';
    out += escapeField(q.album);
    out += '\t';
    out += escapeField(q.albumArtist);
    out += '\t';
    out += std::to_string(q.durationSeconds);
    out += '\t';
    out += std::to_string(q.playbackSeconds);
    out += '\t';
    out += std::to_string((long long)q.startTimestamp);
    out += '\t';
    out += q.refreshOnSubmit ? "1" : "0";
    out += '\t';
    out += std::to_string(q.retryCount);
    out += '\t';
    out += std::to_string((long long)q.nextRetryTimestamp);
    out += '\t';
    out += std::to_string((unsigned long long)q.id);
    return out;
}

static std::vector<QueuedScrobble> loadPendingScrobblesImpl()
{
    std::vector<QueuedScrobble> result;
    pfc::string8 raw = cfgLastfmPendingScrobbles.get();
    const char* data = raw.c_str();
    if (!data || !*data)
        return result;

    const char* line = data;
    while (*line)
    {
        const char* end = std::strchr(line, '\n');
        std::string row = end ? std::string(line, end - line) : std::string(line);
        line = end ? end + 1 : line + row.size();

        if (row.empty())
            continue;

        std::vector<std::string> parts;
        size_t pos = 0;
        while (true)
        {
            size_t tab = row.find('\t', pos);
            if (tab == std::string::npos)
            {
                parts.push_back(row.substr(pos));
                break;
            }
            parts.push_back(row.substr(pos, tab - pos));
            pos = tab + 1;
        }

        if (parts.size() < 10)
            continue; // minimum legacy row size

        QueuedScrobble q;

        q.artist = unescapeField(parts[0]);
        q.title = unescapeField(parts[1]);
        q.album = unescapeField(parts[2]);

        const bool hasAlbumArtist = (parts.size() >= 11); // new format has 11 columns incl. id
        size_t i = 3;

        if (hasAlbumArtist)
        {
            q.albumArtist = unescapeField(parts[i++]); // parts[3]
        }
        else
        {
            q.albumArtist.clear();
        }

        q.durationSeconds = std::atof(parts[i++].c_str());
        q.playbackSeconds = std::atof(parts[i++].c_str());

        if (parts.size() > i)
            q.startTimestamp = std::atoll(parts[i++].c_str());
        if (parts.size() > i)
            q.refreshOnSubmit = parts[i++] == "1";
        if (parts.size() > i)
            q.retryCount = std::atoi(parts[i++].c_str());
        if (parts.size() > i)
            q.nextRetryTimestamp = std::atoll(parts[i++].c_str());
        if (parts.size() > i)
            q.id = static_cast<std::uint64_t>(std::strtoull(parts[i++].c_str(), nullptr, 10));
        else
            q.id = computeLegacyId(q);

        if (q.id == 0)
            q.id = computeLegacyId(q);

        result.push_back(q);
    }

    return result;
}

static void savePendingScrobblesImpl(const std::vector<QueuedScrobble>& items)
{
    pfc::string8 raw;
    for (const auto& q : items)
    {
        raw += serializeScrobble(q).c_str();
        raw += "\n";
    }
    cfgLastfmPendingScrobbles.set(raw);
}

struct RetryUpdate
{
    std::uint64_t id = 0;
    bool remove = false;
    int newRetryCount = 0;
    std::time_t newNextRetryTimestamp = 0;
};

static std::vector<RetryUpdate>
dispatchAndBuildRetryUpdates(const std::vector<QueuedScrobble>& snapshot, unsigned maxToAttempt,
                             const std::function<bool()>& isShuttingDown, LastfmClient& client,
                             const std::function<void()>& onInvalidSession, int64_t dailyBudget)
{
    const std::time_t nowCheck = std::time(nullptr);

    std::vector<RetryUpdate> updates;
    updates.reserve(maxToAttempt);

    bool invalidSessionSeen = false;
    unsigned attempted = 0;

    for (const auto& q : snapshot)
    {
        if (invalidSessionSeen || attempted >= maxToAttempt)
            break;

        if (q.nextRetryTimestamp > 0 && q.nextRetryTimestamp > nowCheck)
            continue;

        // Final mandatory-tag validation
        if (q.artist.empty() || q.title.empty())
        {
            LFM_INFO("Queue: pending still invalid metadata, deferring.");
            continue;
        }

        ++attempted;

        if (isShuttingDown && isShuttingDown())
            break;

        LastfmTrackInfo t;
        t.artist = q.artist;
        t.title = q.title;
        t.album = q.album;
        t.albumArtist = q.albumArtist;
        t.durationSeconds = q.durationSeconds;

        auto res = client.scrobble(t, q.playbackSeconds, q.startTimestamp);

        RetryUpdate u;
        u.id = q.id;

        if (res == LastfmScrobbleResult::SUCCESS)
        {
            u.remove = true;
            updates.push_back(u);

            if (isShuttingDown && isShuttingDown())
                break;

            // Count only accepted scrobbles against daily budget
            cfgLastfmScrobblesToday.set(cfgLastfmScrobblesToday.get() + 1);

            if (dailyBudget > 0 && cfgLastfmScrobblesToday.get() >= dailyBudget)
                break;

            continue;
        }

        if (res == LastfmScrobbleResult::INVALID_SESSION)
        {
            invalidSessionSeen = true;

            // Avoid thrashing: back off globally until auth is fixed.
            if (onInvalidSession)
                onInvalidSession();
            break;
        }

        const std::time_t nowSchedule = std::time(nullptr);
        u.newRetryCount = std::min(q.retryCount + 1, 100);
        u.newNextRetryTimestamp = nowSchedule + std::min(u.newRetryCount * kRetryStepSeconds, kRetryMaxSeconds);

        updates.push_back(u);
    }

    return updates;
}

static void mergeRetryUpdates(std::vector<QueuedScrobble>& latest, const std::vector<RetryUpdate>& updates)
{
    for (const auto& u : updates)
    {
        for (auto it = latest.begin(); it != latest.end();)
        {
            if (it->id != u.id)
            {
                ++it;
                continue;
            }

            if (u.remove)
                it = latest.erase(it);
            else
            {
                it->retryCount = u.newRetryCount;
                it->nextRetryTimestamp = u.newNextRetryTimestamp;
                ++it;
            }

            // IDs are unique. Once matched, stop scanning.
            break;
        }
    }
}

static bool lastfmDailyBudgetExhausted(const std::function<bool()>& isShuttingDown)
{
    if (isShuttingDown && isShuttingDown())
        return true; // treat as exhausted during shutdown: do nothing

    const std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif

    const int todayStamp = (tmNow.tm_year + 1900) * 10000 + (tmNow.tm_mon + 1) * 100 + tmNow.tm_mday;

    if (cfgLastfmDayStamp.get() != todayStamp)
    {
        if (isShuttingDown && isShuttingDown())
            return true;

        cfgLastfmDayStamp.set(todayStamp);
        cfgLastfmScrobblesToday.set(0);
    }

    const int64_t dailyBudget = static_cast<int64_t>(cfgLastfmDailyBudget.get());
    const int64_t todayCount = static_cast<int64_t>(cfgLastfmScrobblesToday.get());
    if (dailyBudget > 0 && todayCount >= dailyBudget)
        return true;

    return false;
}
} // namespace

LastfmQueue::LastfmQueue(LastfmClient& client, std::function<void()> onInvalidSession)
    : client(client), onInvalidSession(std::move(onInvalidSession))
{
}

void LastfmQueue::refreshPendingScrobbleMetadata(const LastfmTrackInfo& track)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto items = loadPendingScrobblesImpl();

    for (auto it = items.rbegin(); it != items.rend(); ++it)
    {
        if (!it->refreshOnSubmit)
            continue;

        LFM_DEBUG("Queue: refresh metadata");

        // Only overwrite with non-empty values
        if (!track.artist.empty())
            it->artist = track.artist;
        if (!track.title.empty())
            it->title = track.title;
        if (!track.album.empty())
            it->album = track.album;
        if (!track.albumArtist.empty())
            it->albumArtist = track.albumArtist;
        if (track.durationSeconds > 0.0)
            it->durationSeconds = track.durationSeconds;

        savePendingScrobblesImpl(items);
        return;
    }
}

void LastfmQueue::queueScrobbleForRetry(const LastfmTrackInfo& track, double playbackSeconds, bool refreshOnSubmit,
                                        std::time_t startTimestamp)
{
    if (track.artist.empty() || track.title.empty())
        return;

    QueuedScrobble q;
    q.artist = track.artist;
    q.title = track.title;
    q.album = track.album;
    q.albumArtist = track.albumArtist;
    q.durationSeconds = track.durationSeconds;
    q.playbackSeconds = playbackSeconds;
    q.startTimestamp = startTimestamp;
    q.refreshOnSubmit = refreshOnSubmit;
    q.id = nextQueueId();

    std::lock_guard<std::mutex> lock(mutex);
    auto items = loadPendingScrobblesImpl();
    items.push_back(q);
    savePendingScrobblesImpl(items);

    LFM_DEBUG("Queue: queued scrobble, pending=" << (unsigned)items.size());
}

void LastfmQueue::retryQueuedScrobbles()
{
    if (core_api::is_shutting_down())
        return;

    auto isShuttingDown = [this]() -> bool { return shuttingDown_ && shuttingDown_->load(std::memory_order_acquire); };

    // IMPORTANT: do NOT touch cfg_* during shutdown, ever.
    if (isShuttingDown())
        return;

    if (lastfmDailyBudgetExhausted(isShuttingDown))
        return;

    const int64_t dailyBudget = static_cast<int64_t>(cfgLastfmDailyBudget.get());
    const int64_t todayCount = static_cast<int64_t>(cfgLastfmScrobblesToday.get());

    int64_t remaining = (dailyBudget > 0) ? (dailyBudget - todayCount) : INT64_MAX;
    remaining = std::max<int64_t>(0, remaining);

    if (dailyBudget > 0 && remaining <= 0)
        return;

    const unsigned maxToAttempt = (dailyBudget > 0) ? (unsigned)std::min<int64_t>((int64_t)kMaxDispatchBatch, remaining)
                                                    : (unsigned)kMaxDispatchBatch;

    std::vector<QueuedScrobble> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = loadPendingScrobblesImpl();
    }

    if (snapshot.empty())
        return;

    const auto updates =
        dispatchAndBuildRetryUpdates(snapshot, maxToAttempt, isShuttingDown, client, onInvalidSession, dailyBudget);

    if (updates.empty())
        return;

    if (isShuttingDown())
        return;

    std::lock_guard<std::mutex> lock(mutex);
    auto latest = loadPendingScrobblesImpl();

    mergeRetryUpdates(latest, updates);

    if (isShuttingDown())
        return;

    savePendingScrobblesImpl(latest);
    LFM_DEBUG("Queue: merge-save done, pending=" << (unsigned)latest.size());
}

std::size_t LastfmQueue::getPendingScrobbleCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return loadPendingScrobblesImpl().size();
}

bool LastfmQueue::hasDueScrobble(std::time_t now) const
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& q : loadPendingScrobblesImpl())
        if (q.nextRetryTimestamp == 0 || q.nextRetryTimestamp <= now)
            return true;
    return false;
}

void LastfmQueue::clearAll()
{
    std::lock_guard<std::mutex> lock(mutex);
    cfgLastfmPendingScrobbles.set("");
    LFM_INFO("Queue: cleared all pending scrobbles.");
}

bool LastfmQueue::drainEnabled()
{
    return cfgLastfmDrainEnabled.get() != 0;
}

std::chrono::seconds LastfmQueue::drainCooldown()
{
    int64_t cooldown = cfgLastfmDrainCooldownSeconds.get();

    if (cooldown < 0)
        cooldown = 0;
    else if (cooldown > 3600)
        cooldown = 3600;

    return std::chrono::seconds(cooldown);
}
