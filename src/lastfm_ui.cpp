//
//  lastfm_ui.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_ui.h"
#include "lastfm_auth.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <mutex>

static const GUID GUID_CFG_LASTFM_AUTHENTICATED = {
    0xeccc9ee3, 0xedcb, 0x443b, {0x8c, 0xef, 0xaa, 0x1c, 0xc8, 0x28, 0x23, 0xeb}};

static const GUID GUID_CFG_LASTFM_USERNAME = {
    0x4cf9fab7, 0x1288, 0x4dc9, {0xb7, 0x81, 0x54, 0x5c, 0xda, 0xd3, 0xd7, 0x37}};

static const GUID GUID_CFG_LASTFM_SESSION_KEY = {
    0xdcf41b90, 0x9d00, 0x44f1, {0xa6, 0x1d, 0xf2, 0x99, 0x95, 0x27, 0x0e, 0x79}};

static const GUID GUID_CFG_LASTFM_SUSPENDED = {
    0xe09c5dbb, 0x8040, 0x4a89, {0x98, 0xc8, 0x0e, 0x0e, 0x42, 0x27, 0xcb, 0x56}};

static cfg_bool cfgLastfmAuthenticated(GUID_CFG_LASTFM_AUTHENTICATED, false);
static cfg_string cfgLastfmUsername(GUID_CFG_LASTFM_USERNAME, "");
static cfg_string cfgLastfmSessionKey(GUID_CFG_LASTFM_SESSION_KEY, "");
static cfg_bool cfgLastfmSuspended(GUID_CFG_LASTFM_SUSPENDED, false);

static std::mutex authMutex;

LastfmAuthState getAuthState()
{
    std::lock_guard<std::mutex> lock(authMutex);
    LastfmAuthState state;
    state.isAuthenticated = cfgLastfmAuthenticated.get();
    state.username = cfgLastfmUsername.get();
    state.sessionKey = cfgLastfmSessionKey.get();
    state.isSuspended = cfgLastfmSuspended.get();
    return state;
}

void setAuthState(const LastfmAuthState& state)
{
    std::lock_guard<std::mutex> lock(authMutex);
    cfgLastfmAuthenticated.set(state.isAuthenticated);
    cfgLastfmUsername.set(state.username.c_str());
    cfgLastfmSessionKey.set(state.sessionKey.c_str());
    if (state.isAuthenticated)
        cfgLastfmSuspended.set(false);
}

bool isAuthenticated()
{
    std::lock_guard<std::mutex> lock(authMutex);
    return cfgLastfmAuthenticated.get();
}

void clearAuthentication()
{
    LFM_INFO("Clearing cfg state.");

    pfc::string8 user;
    {
        std::lock_guard<std::mutex> lock(authMutex);
        user = cfgLastfmUsername.get();
        cfgLastfmAuthenticated.set(false);
        cfgLastfmUsername.set("");
        cfgLastfmSessionKey.set("");
        cfgLastfmSuspended.set(false);
    }

    pfc::string_formatter f;
    f << "Now authenticated=" << (isAuthenticated() ? 1 : 0) << ", user='" << user << "'";

    LFM_INFO(f.c_str());
}

void clearSuspension()
{
    LFM_INFO("Clearing suspend state.");

    pfc::string8 user;
    {
        std::lock_guard<std::mutex> lock(authMutex);
        cfgLastfmSuspended.set(false);
        user = cfgLastfmUsername.get();
    }

    pfc::string_formatter f;
    f << "Suspended=" << (isSuspended() ? "yes" : "no") << ", user='" << user << "'";

    LFM_INFO(f.c_str());
}

bool isSuspended()
{
    std::lock_guard<std::mutex> lock(authMutex);
    return cfgLastfmSuspended.get();
}

void suspendCurrentUser()
{
    LFM_INFO("Suspending current user.");

    pfc::string8 user;
    {
        std::lock_guard<std::mutex> lock(authMutex);
        cfgLastfmSuspended.set(true);
        user = cfgLastfmUsername.get();
    }

    pfc::string_formatter f;
    f << "Suspended=" << (isSuspended() ? "yes" : "no") << ", user='" << user << "'";

    LFM_INFO(f.c_str());
}
