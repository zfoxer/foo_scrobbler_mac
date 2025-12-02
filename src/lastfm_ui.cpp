//
//  lastfm_ui.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_ui.h"
#include "lastfm_auth.h"
#include "lastfm_scrobble.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

static const GUID guid_cfg_lastfm_authenticated = {
    0xeccc9ee3, 0xedcb, 0x443b, {0x8c, 0xef, 0xaa, 0x1c, 0xc8, 0x28, 0x23, 0xeb}};

static const GUID guid_cfg_lastfm_username = {
    0x4cf9fab7, 0x1288, 0x4dc9, {0xb7, 0x81, 0x54, 0x5c, 0xda, 0xd3, 0xd7, 0x37}};

static const GUID guid_cfg_lastfm_session_key = {
    0xdcf41b90, 0x9d00, 0x44f1, {0xa6, 0x1d, 0xf2, 0x99, 0x95, 0x27, 0x0e, 0x79}};

static const GUID guid_cfg_lastfm_suspended = {
    0xe09c5dbb, 0x8040, 0x4a89, {0x98,0xc8,0x0e,0x0e,0x42,0x27,0xcb,0x56}};

static cfg_bool cfg_lastfm_authenticated(guid_cfg_lastfm_authenticated, false);
static cfg_string cfg_lastfm_username(guid_cfg_lastfm_username, "");
static cfg_string cfg_lastfm_session_key(guid_cfg_lastfm_session_key, "");
static cfg_bool cfg_lastfm_suspended(guid_cfg_lastfm_suspended, false);

lastfm_auth_state lastfm_get_auth_state()
{
    lastfm_auth_state state;
    state.is_authenticated = cfg_lastfm_authenticated.get();
    state.username = cfg_lastfm_username.get();
    state.session_key = cfg_lastfm_session_key.get();
    state.is_suspended = cfg_lastfm_suspended.get();
    return state;
}

void lastfm_set_auth_state(const lastfm_auth_state& state)
{
    cfg_lastfm_authenticated.set(state.is_authenticated);
    cfg_lastfm_username.set(state.username.c_str());
    cfg_lastfm_session_key.set(state.session_key.c_str());
    // Any successful auth clears suspension.
    if (state.is_authenticated)
        cfg_lastfm_suspended.set(false);
}

bool lastfm_is_authenticated()
{
    return cfg_lastfm_authenticated.get();
}


void lastfm_clear_authentication()
{
    LFM_INFO("Clearing cfg state.");

    cfg_lastfm_authenticated.set(false);
    cfg_lastfm_username.set("");
    cfg_lastfm_session_key.set("");
    cfg_lastfm_suspended.set(false);   // Suspension is also dropped

    pfc::string8 user = cfg_lastfm_username.get();
    pfc::string_formatter f;
    f << "Now authenticated=" << (lastfm_is_authenticated() ? 1 : 0)
      << ", user='" << user << "'";

    size_t pending = lastfm_get_pending_scrobble_count();
    if (pending > 0)
    {
        f << ", pending cached scrobbles=" << pending;
    }

    LFM_INFO(f.c_str());
}


void lastfm_clear_suspension()
{
    LFM_INFO("Clearing suspend state.");

    cfg_lastfm_suspended.set(false);

    pfc::string8 user = cfg_lastfm_username.get();
    pfc::string_formatter f;
    f << "Suspended=" << (lastfm_is_suspended() ? "yes" : "no")
      << ", user='" << user << "'";

    size_t pending = lastfm_get_pending_scrobble_count();
    if (pending > 0)
    {
        f << ", pending cached scrobbles=" << pending;
    }

    LFM_INFO(f.c_str());
}

bool lastfm_is_suspended()
{
    return cfg_lastfm_suspended.get();
}

void lastfm_suspend_current_user()
{
    LFM_INFO("Suspending current user.");

    cfg_lastfm_suspended.set(true);

    pfc::string8 user = cfg_lastfm_username.get();
    pfc::string_formatter f;
    f << "Suspended=" << (lastfm_is_suspended() ? "yes" : "no")
      << ", user='" << user << "'";

    size_t pending = lastfm_get_pending_scrobble_count();
    if (pending > 0)
    {
        f << ", pending cached scrobbles=" << pending;
    }

    LFM_INFO(f.c_str());
}
