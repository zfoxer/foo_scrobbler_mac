//
//  lastfm_auth.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

#include "lastfm_types.h"
#include "lastfm_ui.h"

// Starts the browser-based Last.fm auth flow.
// Fills outAuthUrl with the URL the user should be sent to.
bool beginAuth(std::string& outAuthUrl);

// Completes auth given a callback URL (ignored in current flow).
// Updates authState with username + session key on success.
bool completeAuthFromCallbackUrl(const std::string& callbackUrl, LastfmAuthState& authState);

// Clears stored credentials.
void logout();

// Returns true if already requested a token and wait to complete auth.
bool hasPendingToken();
