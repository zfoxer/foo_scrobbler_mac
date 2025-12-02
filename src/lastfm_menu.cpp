//
//  lastfm_menu.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_menu.h"
#include "lastfm_ui.h"
#include "lastfm_auth.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <string>
#include <cstdlib>

static const GUID guid_lastfm_authenticate = {
    0xb2f2b721, 0xdc90, 0x45ee, {0xa5, 0xb6, 0x46, 0x4d, 0xb5, 0x4f, 0x5d, 0x5f}};

static const GUID guid_lastfm_clear_auth = {
    0x4b0a35e9, 0x9f8f, 0x4b3f, {0x9d, 0x0e, 0x2a, 0xbb, 0x47, 0xa4, 0x91, 0x73}};

static const GUID guid_lastfm_menu_group = {
    0x7f4f3aa1, 0x1b7c, 0x4b6e, {0x9a, 0x23, 0x4e, 0x8d, 0x17, 0x39, 0x52, 0x11}};

static const GUID guid_lastfm_suspend = {0x3b5aca2b, 0x731e, 0x4ac4, {0xa3, 0xc5, 0x59, 0x4f, 0xcd, 0x27, 0xea, 0x49}};

static mainmenu_group_popup_factory g_lastfm_menu_group_factory(guid_lastfm_menu_group, mainmenu_groups::playback,
                                                                mainmenu_commands::sort_priority_dontcare, "Last.fm");

namespace
{
static void open_browser_url(const std::string& url)
{
#if defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#else
    LFM_INFO("Open manually: " << url.c_str());
#endif
}
} // Namespace

t_uint32 lastfm_menu::get_command_count()
{
    return cmd_count;
}

GUID lastfm_menu::get_command(t_uint32 index)
{
    switch (index)
    {
    case cmd_authenticate:
        return guid_lastfm_authenticate;
    case cmd_clear_auth:
        return guid_lastfm_clear_auth;
    case cmd_suspend:
        return guid_lastfm_suspend;
    default:
        uBugCheck();
    }
}

void lastfm_menu::get_name(t_uint32 index, pfc::string_base& out)
{
    switch (index)
    {
    case cmd_authenticate:
        out = "Authenticate";
        break;
    case cmd_clear_auth:
        out = "Clear authentication";
        break;
    case cmd_suspend:
        if (lastfm_is_suspended())
            out = "Unsuspend user";
        else
            out = "Suspend user";
        break;
    default:
        uBugCheck();
    }
}

bool lastfm_menu::get_description(t_uint32 index, pfc::string_base& out)
{
    switch (index)
    {
    case cmd_authenticate:
        out = "Authenticate this foobar2000 instance with Last.fm.";
        return true;
    case cmd_clear_auth:
        out = "Clear stored Last.fm authentication/session key.";
        return true;
    case cmd_suspend:
        out = "Suspend user from scrobbling.";
        return true;
    default:
        return false;
    }
}

GUID lastfm_menu::get_parent()
{
    return guid_lastfm_menu_group;
}

t_uint32 lastfm_menu::get_sort_priority()
{
    return sort_priority_dontcare;
}

bool lastfm_menu::get_display(t_uint32 index, pfc::string_base& text, uint32_t& flags)
{
    flags = 0;
    const bool authed = lastfm_is_authenticated();

    switch (index)
    {
    case cmd_authenticate:
        if (authed)
            return false; // Hide it entirely
        get_name(index, text);
        break;

    case cmd_suspend:
        if (!authed)
            return false; // Hide it entirely
        get_name(index, text);
        break;

    case cmd_clear_auth:
        if (!authed)
            return false; // hide when no authenticated user
        get_name(index, text);
        break;

    default:
        return false;
    }

    return true;
}

void lastfm_menu::execute(t_uint32 index, ctx_t /*Callback*/)
{
    switch (index)
    {
    case cmd_authenticate:
    {
        if (lastfm_is_authenticated())
            return;

        if (!lastfm_has_pending_token())
        {
            std::string url;
            if (lastfm_begin_auth(url) && !url.empty())
            {
                LFM_INFO("Starting authentication: " << url.c_str());
                popup_message::g_show(
                    "A browser window will open to authorize this foobar2000 instance with Last.fm.\n\n"
                    "1) Log in if requested.\n"
                    "2) Click \"Allow access\".\n"
                    "3) When finished, return to foobar2000 and click \"Authenticate\" again to complete setup.",
                    "Last.fm Scrobbler");

                open_browser_url(url);
            }
            else
            {
                popup_message::g_show("Failed to start Last.fm authentication.\n"
                                      "Check API key/secret settings.",
                                      "Last.fm Scrobbler");
            }
        }
        else
        {
            lastfm_auth_state state;
            if (lastfm_complete_auth_from_callback_url("", state))
            {
                pfc::string8 msg;
                msg << "Authenticated as: " << state.username.c_str();
                popup_message::g_show(msg, "Last.fm Scrobbler");
                LFM_INFO("Authenticated as: " << state.username.c_str());
            }
            else
            {
                popup_message::g_show("Could not complete authentication.\n"
                                      "Ensure you clicked \"Allow access\" in your browser, then try again.",
                                      "Last.fm Scrobbler");
            }
        }
        break;
    }

    case cmd_clear_auth:
        lastfm_clear_authentication();
        popup_message::g_show("Stored Last.fm authentication has been cleared.", "Last.fm");
        break;
    case cmd_suspend:
        if (!lastfm_is_authenticated())
            return;

        if (lastfm_is_suspended())
        {
            lastfm_clear_suspension();
            LFM_INFO("User unsuspended. Scrobbling is enabled again.");
        }
        else
        {
            lastfm_suspend_current_user();
            LFM_INFO("User suspended. Scrobbles will be skipped while suspended.");
        }
        break;
    default:
        uBugCheck();
    }
}

// Static registration of command provider
static mainmenu_commands_factory_t<lastfm_menu> g_lastfm_menu_factory;
