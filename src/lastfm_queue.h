//
//  lastfm_queue.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <cstddef>
#include <ctime>
#include <functional>
#include <mutex>

#include "lastfm_track_info.h"

class LastfmClient;

// Persistent scrobble queue
class LastfmQueue
{
  public:
    explicit LastfmQueue(LastfmClient& client, std::function<void()> onInvalidSession = {});

    void refreshPendingScrobbleMetadata(const LastfmTrackInfo& track);

    void queueScrobbleForRetry(const LastfmTrackInfo& track, double playbackSeconds, bool refreshOnSubmit,
                               std::time_t startTimestamp);

    void retryQueuedScrobbles();
    void retryQueuedScrobblesAsync();

    std::size_t getPendingScrobbleCount() const;

  private:
    LastfmClient& client;
    std::function<void()> onInvalidSession;
    mutable std::mutex mutex;
};
