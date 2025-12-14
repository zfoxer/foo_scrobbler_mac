//
//  lastfm_queue.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>
#include <cstddef>
#include <ctime>
#include <functional>
#include <mutex>

#include "lastfm_types.h"  // LastfmTrackInfo
#include "lastfm_client.h" // LastfmClient

class LastfmQueue
{
  public:
    LastfmQueue(LastfmClient& client, std::function<void()> onInvalidSession);

    // Called when metadata changes before submit
    void refreshPendingScrobbleMetadata(const LastfmTrackInfo& track);

    // Queue a scrobble for retry
    void queueScrobbleForRetry(const LastfmTrackInfo& track, double playbackSeconds, bool refreshOnSubmit,
                               std::time_t startTimestamp);

    // Retry logic
    void retryQueuedScrobbles();
    void retryQueuedScrobblesAsync();

    // Introspection
    std::size_t getPendingScrobbleCount() const;
    bool hasDueScrobble(std::time_t now) const;

    bool isRetryInFlight() const
    {
        return retryInFlight.load();
    }

  private:
    LastfmClient& client;
    std::function<void()> onInvalidSession;

    mutable std::mutex mutex;
    std::atomic<bool> retryInFlight{false};
};
