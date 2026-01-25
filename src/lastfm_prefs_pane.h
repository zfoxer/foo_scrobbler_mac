//
//  lastfm_prefs_pane.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

// Advanced Preferences registration (Preferences → Advanced).
void lastfmRegisterPrefsPane();

// Advanced → Tools → Foo Scrobbler → Scrobbling
bool lastfmOnlyScrobbleFromMediaLibrary();

// 0 = No dynamic sources, 1 = NP only, 2 = NP & scrobbling
int lastfmDynamicSourcesMode();

bool lastfmDisableNowPlaying();
