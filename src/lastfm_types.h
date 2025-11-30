//
//  lastfm_types.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <string>

namespace lastfm_plugin
{

struct lastfm_auth_state
{
    bool is_authenticated = false;
    std::string username;    // Last.fm username
    std::string session_key; // 'sk' from Last.fm
};

} // namespace lastfm_plugin

using lastfm_plugin::lastfm_auth_state; // easier usage in UI code
