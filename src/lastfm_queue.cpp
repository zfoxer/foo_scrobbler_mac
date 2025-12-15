//
//  lastfm_queue.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_queue.h"

#include "debug.h"
#include "lastfm_client.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

namespace
{
static const GUID GUID_CFG_LASTFM_PENDING_SCROBBLES = {
    0x9b3b2c41, 0x4c2d, 0x4d8f, {0x9a, 0xa1, 0x1e, 0x37, 0x5b, 0x6a, 0x82, 0x19}};

static cfg_string cfgLastfmPendingScrobbles(GUID_CFG_LASTFM_PENDING_SCROBBLES, "");

// Dispatch at most 10 per run
static constexpr size_t kMaxDispatchBatch = 10;

// Linear backoff: 60s, 120s, 180s… capped
static constexpr int kRetryStepSeconds = 60;
static constexpr int kRetryMaxSeconds = 60 * 60; // 1h cap

struct QueuedScrobble
{
    std::string artist;
    std::string title;
    std::string album;
    double durationSeconds = 0.0;
    double playbackSeconds = 0.0;
    std::time_t startTimestamp = 0;
    bool refreshOnSubmit = false;

    int retryCount = 0;
    std::time_t nextRetryTimestamp = 0;
};

static std::string serializeScrobble(const QueuedScrobble& q)
{
    std::string out;
    out += q.artist;
    out += '\t';
    out += q.title;
    out += '\t';
    out += q.album;
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

        if (parts.size() < 5)
            continue;

        QueuedScrobble q;
        q.artist = parts[0];
        q.title = parts[1];
        q.album = parts[2];
        q.durationSeconds = std::atof(parts[3].c_str());
        q.playbackSeconds = std::atof(parts[4].c_str());
        if (parts.size() > 5)
            q.startTimestamp = std::atoll(parts[5].c_str());
        if (parts.size() > 6)
            q.refreshOnSubmit = parts[6] == "1";
        if (parts.size() > 7)
            q.retryCount = std::atoi(parts[7].c_str());
        if (parts.size() > 8)
            q.nextRetryTimestamp = std::atoll(parts[8].c_str());

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

static bool sameKey(const QueuedScrobble& a, const QueuedScrobble& b)
{
    return a.startTimestamp == b.startTimestamp && a.artist == b.artist && a.title == b.title && a.album == b.album;
}

struct RetryUpdate
{
    QueuedScrobble key;
    bool remove = false;
    int newRetryCount = 0;
    std::time_t newNextRetryTimestamp = 0;
};
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
    q.durationSeconds = track.durationSeconds;
    q.playbackSeconds = playbackSeconds;
    q.startTimestamp = startTimestamp;
    q.refreshOnSubmit = refreshOnSubmit;

    std::lock_guard<std::mutex> lock(mutex);
    auto items = loadPendingScrobblesImpl();
    items.push_back(q);
    savePendingScrobblesImpl(items);

    LFM_DEBUG("Queue: queued scrobble, pending=" << (unsigned)items.size());
}

void LastfmQueue::retryQueuedScrobbles()
{
    std::vector<QueuedScrobble> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = loadPendingScrobblesImpl();
    }

    if (snapshot.empty())
        return;

    const std::time_t nowCheck = std::time(nullptr);
    std::vector<RetryUpdate> updates;
    updates.reserve(kMaxDispatchBatch);

    bool invalidSessionSeen = false;
    unsigned attempted = 0;

    for (const auto& q : snapshot)
    {
        if (invalidSessionSeen || attempted >= kMaxDispatchBatch)
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

        LastfmTrackInfo t{q.artist, q.title, q.album, "", q.durationSeconds};
        auto res = client.scrobble(t, q.playbackSeconds, q.startTimestamp);

        RetryUpdate u;
        u.key = q;

        if (res == LastfmScrobbleResult::SUCCESS)
        {
            u.remove = true;
            updates.push_back(u);
            continue;
        }

        if (res == LastfmScrobbleResult::INVALID_SESSION)
        {
            invalidSessionSeen = true;
            if (onInvalidSession)
                onInvalidSession();
            break;
        }

        const std::time_t nowSchedule = std::time(nullptr);
        u.newRetryCount = std::min(q.retryCount + 1, 100);
        u.newNextRetryTimestamp = nowSchedule + std::min(u.newRetryCount * kRetryStepSeconds, kRetryMaxSeconds);

        updates.push_back(u);
    }

    if (updates.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex);
    auto latest = loadPendingScrobblesImpl();

    for (const auto& u : updates)
    {
        for (auto it = latest.begin(); it != latest.end();)
        {
            if (!sameKey(*it, u.key))
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
        }
    }

    savePendingScrobblesImpl(latest);
    LFM_DEBUG("Queue: merge-save done, pending=" << (unsigned)latest.size());
}

void LastfmQueue::retryQueuedScrobblesAsync()
{
    bool expected = false;
    if (!retryInFlight.compare_exchange_strong(expected, true))
        return;

    std::thread(
        [this]()
        {
            retryQueuedScrobbles();
            retryInFlight.store(false);
        })
        .detach();
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
