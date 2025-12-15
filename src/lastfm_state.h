//
//  lastfm_state.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include "lastfm_types.h"

// cfg-backed state (no UI, no dialogs)

// Returns the current cached auth state (from config)
LastfmAuthState lastfm_get_auth_state();

// Used by the auth layer to persist successful auth.
void lastfm_set_auth_state(const LastfmAuthState& state);

bool lastfm_is_authenticated();
bool lastfm_is_suspended();

// Mutators
void lastfm_clear_authentication();
void lastfm_clear_suspension();
void lastfm_suspend_current_user();
