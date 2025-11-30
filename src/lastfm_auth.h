//
//  lastfm_auth.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <string>

#include "lastfm_types.h"
#include "lastfm_ui.h"

// Starts the browser-based Last.fm auth flow.
// Fills out_auth_url with the URL the user should be sent to.
bool lastfm_begin_auth(std::string& out_auth_url);

// Completes auth given a callback URL (ignored in current simplified flow).
// Updates auth_state with username + session_key on success.
bool lastfm_complete_auth_from_callback_url(const std::string& callback_url, lastfm_auth_state& auth_state);

// Clears stored credentials.
void lastfm_logout();

// Returns true if we already requested a token and are waiting to complete auth.
bool lastfm_has_pending_token();
