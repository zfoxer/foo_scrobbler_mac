//
//  lastfm_ui.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <string>

#include "lastfm_types.h"

// Returns the current cached auth state (from config)
lastfm_auth_state lastfm_get_auth_state();

// Main entry point for the popup window.
// Called from the foobar2000 main menu command.
void show_lastfm_scrobbler_dialog();

// New helpers used by the menu.
bool lastfm_is_authenticated();
void show_lastfm_auth_dialog();
void clear_lastfm_authentication();

// Used by the auth layer to persist successful auth.
void lastfm_set_auth_state(const lastfm_auth_state& state);
