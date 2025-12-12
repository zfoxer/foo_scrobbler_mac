//
//  lastfm_scrobbler.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <ctime>

#include "lastfm_track_info.h"
#include "lastfm_queue.h"

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

  private:
    void handleInvalidSessionOnce();

  private:
    LastfmClient& client;
    LastfmQueue queue;
    bool invalidSessionHandled = false;
};
