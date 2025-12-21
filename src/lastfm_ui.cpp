//
//  lastfm_ui.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_ui.h"
#include "lastfm_state.h"
#include "lastfm_prefs_pane_state.h"

#include <foobar2000/SDK/advconfig.h>
#include <foobar2000/SDK/foobar2000.h>

// Reuse the exact GUID from lastfm_prefs_pane.cpp:
static const GUID GUID_LASTFM_PREFS_CHECKBOX_0 = {
    0x290f410b, 0x7c8d, 0x45b7, {0x8b, 0x2e, 0x8d, 0xd4, 0xf9, 0x41, 0x5a, 0x82}};

bool lastfm_disable_nowplaying()
{
    service_ptr_t<advconfig_entry_checkbox> e;
    if (!advconfig_entry::g_find_t(e, GUID_LASTFM_PREFS_CHECKBOX_0))
        return false; // default = enabled
    return e->get_state();
}

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

int getPrefsPaneRadioChoice()
{
    return lastfm_get_prefs_pane_radio_choice();
}

void setPrefsPaneRadioChoice(int value)
{
    lastfm_set_prefs_pane_radio_choice(value);
}

bool getPrefsPaneCheckbox()
{
    return lastfm_get_prefs_pane_checkbox();
}

void setPrefsPaneCheckbox(bool enabled)
{
    lastfm_set_prefs_pane_checkbox(enabled);
}
