//
//  lastfm_ui.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

#include "lastfm_types.h"

// NOTE: State is implemented in lastfm_state.* now.
LastfmAuthState getAuthState();
bool isAuthenticated();
bool isSuspended();
void clearAuthentication();
void clearSuspension();
void suspendCurrentUser();

// UI entry points (keep as-is if you have them elsewhere)
void showScrobblerDialog();
void showAuthDialog();

// Used by the auth layer to persist successful auth.
void setAuthState(const LastfmAuthState& state);
