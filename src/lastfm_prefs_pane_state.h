//
//  lastfm_prefs_pane_state.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

// cfg-backed preferences

// 3-choice radio: valid range [0..2]
int lastfmGetPrefsPaneRadioChoice();
void lastfmSetPrefsPaneRadioChoice(int value);

// boolean checkbox
bool lastfmGetPrefsPaneCheckbox();
void lastfmSetPrefsPaneCheckbox(bool enabled);
