//
//  lastfm_web_api.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <ctime>

#include "lastfm_results.h"
#include "lastfm_track_info.h"

class LastfmWebApi
{
  public:
    bool updateNowPlaying(const LastfmTrackInfo& track);
    LastfmScrobbleResult scrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startTimestamp);
};
