//
//  lastfm_queue.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>
#include <cstddef>
#include <ctime>
#include <functional>
#include <mutex>

#include "lastfm_auth_state.h"
#include "lastfm_client.h"

class LastfmQueue
{
  public:
    static constexpr const char* QUEUE_VERSION = "#FSQ2";
    LastfmQueue(LastfmClient& client, std::function<void()> onInvalidSession);

    void setShuttingDownFlag(std::atomic<bool>* flag)
    {
        shuttingDown_ = flag;
    }

    // Called when metadata changes before submit
    void refreshPendingScrobbleMetadata(const LastfmTrackInfo& track);

    // Queue a scrobble for retry
    void queueScrobbleForRetry(const LastfmTrackInfo& track, double playbackSeconds, bool refreshOnSubmit,
                               std::time_t startTimestamp);

    // Retry logic
    void retryQueuedScrobbles();

    // Introspection
    std::size_t getPendingScrobbleCount() const;
    bool hasDueScrobble(std::time_t now);

    // Clear all pending scrobbles (persistent storage).
    void clearAll();

    static bool drainEnabled();
    static std::chrono::seconds drainCooldown();

  private:
    void enterRateLimitCooldownLocked(std::time_t now, std::time_t cooldownSeconds);
    bool isRateLimitedLocked(std::time_t now);
    std::atomic<bool>* shuttingDown_ = nullptr;
    LastfmClient& client;
    std::function<void()> onInvalidSession;

    mutable std::mutex mutex;
    std::time_t rateLimitedUntil_ = 0;
    bool rateLimitLogged_ = false;
};
