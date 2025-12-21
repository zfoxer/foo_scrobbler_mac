//
//  lastfm_prefs_pane_state.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

// cfg-backed preferences

// 3-choice radio: valid range [0..2]
int lastfm_get_prefs_pane_radio_choice();
void lastfm_set_prefs_pane_radio_choice(int value);

// boolean checkbox
bool lastfm_get_prefs_pane_checkbox();
void lastfm_set_prefs_pane_checkbox(bool enabled);
