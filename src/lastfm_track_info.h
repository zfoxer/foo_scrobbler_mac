//
//  lastfm_track_info.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

struct LastfmTrackInfo
{
    std::string artist;
    std::string title;
    std::string album;
    std::string mbid;
    std::string albumArtist;
    double durationSeconds = 0.0;
};
