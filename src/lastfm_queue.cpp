//
//  lastfm_queue.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_queue.h"

#include "lastfm_client.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
static const GUID GUID_CFG_LASTFM_PENDING_SCROBBLES = {
    0x9b3b2c41, 0x4c2d, 0x4d8f, {0x9a, 0xa1, 0x1e, 0x37, 0x5b, 0x6a, 0x82, 0x19}};

static cfg_string cfgLastfmPendingScrobbles(GUID_CFG_LASTFM_PENDING_SCROBBLES, "");

static const unsigned MAX_BATCH = 10;

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
        if (it->refreshOnSubmit)
        {
            it->artist = track.artist;
            it->title = track.title;
            it->album = track.album;
            it->durationSeconds = track.durationSeconds;
            savePendingScrobblesImpl(items);
            break;
        }
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
}

void LastfmQueue::retryQueuedScrobbles()
{
    std::vector<QueuedScrobble> items;
    {
        std::lock_guard<std::mutex> lock(mutex);
        items = loadPendingScrobblesImpl();
    }

    if (items.empty())
        return;

    std::vector<QueuedScrobble> remaining;
    bool invalidSessionSeen = false;
    unsigned processed = 0;

    std::time_t now = std::time(nullptr);
    (void)now;

    for (const auto& q : items)
    {
        if (invalidSessionSeen || processed >= MAX_BATCH)
        {
            remaining.push_back(q);
            continue;
        }

        LastfmTrackInfo t{q.artist, q.title, q.album, "", q.durationSeconds};
        ++processed;

        auto res = client.scrobble(t, q.playbackSeconds, q.startTimestamp);

        if (res == LastfmScrobbleResult::SUCCESS)
            continue;

        if (res == LastfmScrobbleResult::INVALID_SESSION)
        {
            invalidSessionSeen = true;
            remaining.push_back(q);
            if (onInvalidSession)
                onInvalidSession();
            continue;
        }

        remaining.push_back(q);
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        savePendingScrobblesImpl(remaining);
    }
}

void LastfmQueue::retryQueuedScrobblesAsync()
{
    std::thread([this]() { retryQueuedScrobbles(); }).detach();
}

std::size_t LastfmQueue::getPendingScrobbleCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return loadPendingScrobblesImpl().size();
}
