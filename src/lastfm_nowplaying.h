//
//  lastfm_nowplaying.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

// Sends a Last.fm track.updateNowPlaying request for the given track.
// Returns true if the HTTP request completed successfully (not necessarily
// that Last.fm accepted it), false on network/HTTP failure.
bool sendNowPlaying(const std::string& artist, const std::string& title, const std::string& album,
                    double durationSeconds);
