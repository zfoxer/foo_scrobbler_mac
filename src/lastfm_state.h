//
//  lastfm_state.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include "lastfm_types.h"

// cfg-backed state (no UI, no dialogs)

// Returns the current cached auth state (from config)
LastfmAuthState lastfmGetAuthState();

// Used by the auth layer to persist successful auth.
void lastfmSetAuthState(const LastfmAuthState& state);

bool lastfmIsAuthenticated();
bool lastfmIsSuspended();

// Mutators
void lastfmClearAuthentication();
void lastfmClearSuspension();
void lastfmSuspendCurrentUser();

// Queue ownership (prevents cross-account submission)
pfc::string8 lastfmGetQueueOwnerUsername();
void lastfmSetQueueOwnerUsername(const char* username);
void lastfmClearQueueOwnerUsername();
