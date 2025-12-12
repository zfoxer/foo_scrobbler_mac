//
//  lastfm_ui.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

#include "lastfm_types.h"

LastfmAuthState getAuthState();

void showScrobblerDialog();

bool isAuthenticated();
void showAuthDialog();
void clearAuthentication();
void clearSuspension();
bool isSuspended();
void suspendCurrentUser();

void setAuthState(const LastfmAuthState& state);
