//
//  lastfm_ui.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_ui.h"
#include "lastfm_state.h"
#include "lastfm_prefs_pane_state.h"

#include <foobar2000/SDK/advconfig.h>
#include <foobar2000/SDK/foobar2000.h>

// Reuse the exact GUID from lastfm_prefs_pane.cpp:
static const GUID GUID_LASTFM_PREFS_CHECKBOX_0 = {
    0x290f410b, 0x7c8d, 0x45b7, {0x8b, 0x2e, 0x8d, 0xd4, 0xf9, 0x41, 0x5a, 0x82}};

bool lastfmDisableNowplaying()
{
    service_ptr_t<advconfig_entry_checkbox> e;
    if (!advconfig_entry::g_find_t(e, GUID_LASTFM_PREFS_CHECKBOX_0))
        return false; // default = enabled
    return e->get_state();
}

LastfmAuthState getAuthState()
{
    return lastfmGetAuthState();
}

void setAuthState(const LastfmAuthState& state)
{
    lastfmSetAuthState(state);
}

bool isAuthenticated()
{
    return lastfmIsAuthenticated();
}

void clearAuthentication()
{
    lastfmClearAuthentication();
}

void clearSuspension()
{
    lastfmClearSuspension();
}

bool isSuspended()
{
    return lastfmIsSuspended();
}

void suspendCurrentUser()
{
    lastfmSuspendCurrentUser();
}

int getPrefsPaneRadioChoice()
{
    return lastfmGetPrefsPaneRadioChoice();
}

void setPrefsPaneRadioChoice(int value)
{
    lastfmSetPrefsPaneRadioChoice(value);
}

bool getPrefsPaneCheckbox()
{
    return lastfmGetPrefsPaneCheckbox();
}

void setPrefsPaneCheckbox(bool enabled)
{
    lastfmSetPrefsPaneCheckbox(enabled);
}
