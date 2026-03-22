//
//  lastfm_auth_state.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

struct LastfmAuthState
{
    bool isAuthenticated = false;
    bool isSuspended = false;
    std::string username;   // Last.fm username
    std::string sessionKey; // 'sk' from Last.fm
};
