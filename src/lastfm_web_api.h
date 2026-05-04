//
//  lastfm_web_api.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <ctime>
#include <vector>

#include "lastfm_scrobble_result.h"
#include "lastfm_track_info.h"

struct LastfmScrobbleRequest
{
    LastfmTrackInfo track;
    double playbackSeconds = 0.0;
    std::time_t startTimestamp = 0;
};

class LastfmWebApi
{
  public:
    bool updateNowPlaying(const LastfmTrackInfo& track);
    LastfmScrobbleResult scrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startTimestamp);
    LastfmScrobbleResult scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests);
};
