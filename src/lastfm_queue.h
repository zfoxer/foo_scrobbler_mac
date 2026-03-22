//
//  lastfm_queue.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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
    struct QueuedScrobble
    {
        std::uint64_t id = 0;
        std::string artist;
        std::string title;
        std::string album;
        std::string albumArtist;
        std::string mbid;
        double durationSeconds = 0.0;
        double playbackSeconds = 0.0;
        std::time_t startTimestamp = 0;
        bool refreshOnSubmit = false;
        int retryCount = 0;
        int otherErrorCount = 0;
        std::time_t nextRetryTimestamp = 0;
    };

    struct RetryUpdate
    {
        std::uint64_t id = 0;
        bool remove = false;
        int newRetryCount = 0;
        int newOtherErrorCount = 0;
        std::time_t newNextRetryTimestamp = 0;
    };

    struct DispatchOutcome
    {
        std::vector<RetryUpdate> updates;
        bool rateLimited = false;
    };

    void ensureCacheLoadedLocked() const;
    void saveCacheLocked();

    static std::string escapeField(const std::string& in);
    static std::string unescapeField(const std::string& in);
    static std::string serializeScrobble(const QueuedScrobble& q);

    static DispatchOutcome
    dispatchAndBuildRetryUpdates(const std::vector<QueuedScrobble>& snapshot, unsigned maxToAttempt,
                                 const std::function<bool()>& isShuttingDown, LastfmClient& client,
                                 const std::function<void()>& onInvalidSession, int64_t dailyBudget);
    static void mergeRetryUpdates(std::vector<QueuedScrobble>& latest, const std::vector<RetryUpdate>& updates);

    void enterRateLimitCooldownLocked(std::time_t now, std::time_t cooldownSeconds);
    bool isRateLimitedLocked(std::time_t now);
    std::atomic<bool>* shuttingDown_ = nullptr;
    LastfmClient& client;
    std::function<void()> onInvalidSession;

    mutable std::mutex mutex;
    mutable std::vector<QueuedScrobble> cache_;
    mutable bool cacheLoaded_ = false;
    std::time_t rateLimitedUntil_ = 0;
    bool rateLimitLogged_ = false;
};
