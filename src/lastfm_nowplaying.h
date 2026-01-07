//
//  lastfm_nowplaying.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

// Sends a Last.fm track.updateNowPlaying request for the given track.
// Returns true if Last.fm responds with valid JSON and no API error.
// Returns false on HTTP failure, invalid JSON, or API error.
bool sendNowPlaying(const std::string& artist, const std::string& title, const std::string& album,
                    double durationSeconds);
