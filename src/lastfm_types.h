//
//  lastfm_types.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

namespace LastfmPlugin
{

struct LastfmAuthState
{
    bool isAuthenticated = false;
    bool isSuspended = false;
    std::string username;   // Last.fm username
    std::string sessionKey; // 'sk' from Last.fm
};

} // namespace LastfmPlugin

using LastfmPlugin::LastfmAuthState;
