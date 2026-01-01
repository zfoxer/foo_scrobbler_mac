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

class LastfmClient;

class LastfmScrobbler
{
  public:
    explicit LastfmScrobbler(LastfmClient& client);

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
    void deferNowPlayingAfterRetry(const LastfmTrackInfo& track);

  private:
    LastfmClient& client;
    LastfmQueue queue;
    bool invalidSessionHandled = false;
    // If NowPlaying is skipped due to retry-in-flight, defer it and send once retry completes.
    std::atomic<uint64_t> nowPlayingEpoch{0};
    mutable std::mutex nowPlayingMutex;
    LastfmTrackInfo deferredNowPlayingTrack;
};
