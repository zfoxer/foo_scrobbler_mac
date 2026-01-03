//
//  lastfm_scrobbler.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <ctime>
#include <atomic>
#include <mutex>

#include "lastfm_queue.h"
#include "lastfm_track_info.h"
#include "lastfm_worker.h"

class LastfmClient;
class LastfmWorker;

class LastfmScrobbler
{
  public:
    explicit LastfmScrobbler(LastfmClient& client);
    ~LastfmScrobbler();

    void shutdown();
    void onNowPlaying(const LastfmTrackInfo& track);
    void sendNowPlayingOnly(const LastfmTrackInfo& track);
    void refreshPendingMetadata(const LastfmTrackInfo& track);

    void queueScrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startWallclock,
                       bool refreshOnSubmit);

    void retryAsync();
    void clearQueue();
    void resetInvalidSessionHandling();
    void onAuthenticationRecovered();

  private:
    void handleInvalidSessionOnce();
    void dispatchRetryIfDue(const char* reasonTag);

  private:
    LastfmClient& client;
    LastfmQueue queue;
    LastfmWorker worker; //  Kepp the order for proper destruction later.
    std::atomic<bool> invalidSessionHandled{false};
    std::atomic<bool> shuttingDown{false};
};
