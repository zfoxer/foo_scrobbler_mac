//
//  lastfm_menu.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_menu.h"
#include "lastfm_authenticator.h"
#include "lastfm_ui.h"
#include "lastfm_client.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <string>
#include <cstdlib>

static const GUID GUID_LASTFM_AUTHENTICATE = {
    0xb2f2b721, 0xdc90, 0x45ee, {0xa5, 0xb6, 0x46, 0x4d, 0xb5, 0x4f, 0x5d, 0x5f}};

static const GUID GUID_LASTFM_CLEAR_AUTH = {
    0x4b0a35e9, 0x9f8f, 0x4b3f, {0x9d, 0x0e, 0x2a, 0xbb, 0x47, 0xa4, 0x91, 0x73}};

static const GUID GUID_LASTFM_MENU_GROUP = {
    0x7f4f3aa1, 0x1b7c, 0x4b6e, {0x9a, 0x23, 0x4e, 0x8d, 0x17, 0x39, 0x52, 0x11}};

static const GUID GUID_LASTFM_SUSPEND = {0x3b5aca2b, 0x731e, 0x4ac4, {0xa3, 0xc5, 0x59, 0x4f, 0xcd, 0x27, 0xea, 0x49}};

static mainmenu_group_popup_factory lastfmMenuGroupFactory(GUID_LASTFM_MENU_GROUP, mainmenu_groups::playback,
                                                           mainmenu_commands::sort_priority_dontcare, "Last.fm");

namespace
{
static LastfmClient lastfmClient;
static Authenticator authenticator(lastfmClient);

static void openBrowserUrl(const std::string& url)
{
#if defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#else
    LFM_INFO("Open manually: " << url.c_str());
#endif
}
} // namespace

t_uint32 LastfmMenu::get_command_count()
{
    return CMD_COUNT;
}

GUID LastfmMenu::get_command(t_uint32 index)
{
    switch (index)
    {
    case CMD_AUTHENTICATE:
        return GUID_LASTFM_AUTHENTICATE;
    case CMD_CLEAR_AUTH:
        return GUID_LASTFM_CLEAR_AUTH;
    case CMD_SUSPEND:
        return GUID_LASTFM_SUSPEND;
    default:
        uBugCheck();
    }
}

void LastfmMenu::get_name(t_uint32 index, pfc::string_base& out)
{
    switch (index)
    {
    case CMD_AUTHENTICATE:
        out = "Authenticate";
        break;
    case CMD_CLEAR_AUTH:
        out = "Clear authentication";
        break;
    case CMD_SUSPEND:
        out = isSuspended() ? "Resume scrobbling" : "Pause scrobbling";
        break;
    default:
        uBugCheck();
    }
}

bool LastfmMenu::get_description(t_uint32 index, pfc::string_base& out)
{
    switch (index)
    {
    case CMD_AUTHENTICATE:
        out = "Authenticate this foobar2000 instance with Last.fm.";
        return true;
    case CMD_CLEAR_AUTH:
        out = "Clear stored Last.fm authentication/session key.";
        return true;
    case CMD_SUSPEND:
        out = "Suspend user from scrobbling.";
        return true;
    default:
        return false;
    }
}

GUID LastfmMenu::get_parent()
{
    return GUID_LASTFM_MENU_GROUP;
}

t_uint32 LastfmMenu::get_sort_priority()
{
    return sort_priority_dontcare;
}

bool LastfmMenu::get_display(t_uint32 index, pfc::string_base& text, uint32_t& flags)
{
    flags = 0;
    const bool authed = isAuthenticated();

    switch (index)
    {
    case CMD_AUTHENTICATE:
        if (authed)
            return false;
        break;
    case CMD_CLEAR_AUTH:
    case CMD_SUSPEND:
        if (!authed)
            return false;
        break;
    default:
        return false;
    }

    get_name(index, text);
    return true;
}

void LastfmMenu::execute(t_uint32 index, ctx_t)
{
    switch (index)
    {
    case CMD_AUTHENTICATE:
    {
        if (isAuthenticated())
            return;

        std::string url;
        if (!authenticator.hasPendingToken())
        {
            if (authenticator.startAuth(url))
            {
                popup_message::g_show("A browser window will open to authorize this foobar2000 instance with Last.fm.\n"
                                      "After allowing access, return here and click Authenticate again.",
                                      "Last.fm Scrobbler");
                openBrowserUrl(url);
            }
            else
            {
                popup_message::g_show("Failed to start authentication.", "Last.fm Scrobbler");
            }
        }
        else
        {
            LastfmAuthState state;
            if (authenticator.completeAuth(state))
            {
                popup_message::g_show("Authentication complete.", "Last.fm Scrobbler");
            }
            else
            {
                popup_message::g_show("Authentication failed.", "Last.fm Scrobbler");
            }
        }
        break;
    }

    case CMD_CLEAR_AUTH:
        authenticator.logout();
        popup_message::g_show("Stored Last.fm authentication has been cleared.", "Last.fm");
        break;

    case CMD_SUSPEND:
        if (isSuspended())
            clearSuspension();
        else
            suspendCurrentUser();
        break;

    default:
        uBugCheck();
    }
}

static mainmenu_commands_factory_t<LastfmMenu> lastfmMenuFactory;
