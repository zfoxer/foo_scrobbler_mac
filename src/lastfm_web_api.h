//
//  lastfm_web_api.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "lastfm_scrobble_result.h"
#include "lastfm_track_info.h"

struct LastfmScrobbleRequest
{
    LastfmTrackInfo track;
    double playbackSeconds = 0.0;
    std::time_t startTimestamp = 0;
};

struct LastfmTrackOutcome
{
    bool accepted = true;
    int ignoredCode = 0;
    std::string ignoredText;
};

class LastfmWebApi
{
  public:
    LastfmScrobbleResult updateNowPlaying(const LastfmTrackInfo& track, abort_callback& abort);
    LastfmScrobbleResult scrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startTimestamp,
                                  abort_callback& abort, LastfmTrackOutcome* outOutcome = nullptr);
    LastfmScrobbleResult scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests, abort_callback& abort,
                                       std::vector<LastfmTrackOutcome>* outPerTrack = nullptr);
};
