//
//  lastfm_ui.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_ui.h"
#include "lastfm_state.h"

LastfmAuthState getAuthState()
{
    return lastfm_get_auth_state();
}

void setAuthState(const LastfmAuthState& state)
{
    lastfm_set_auth_state(state);
}

bool isAuthenticated()
{
    return lastfm_is_authenticated();
}

void clearAuthentication()
{
    lastfm_clear_authentication();
}

void clearSuspension()
{
    lastfm_clear_suspension();
}

bool isSuspended()
{
    return lastfm_is_suspended();
}

void suspendCurrentUser()
{
    lastfm_suspend_current_user();
}
