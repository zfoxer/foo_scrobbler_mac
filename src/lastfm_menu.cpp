//
//  lastfm_menu.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_menu.h"
#include "lastfm_core.h"
#include "lastfm_state.h"
#include "lastfm_settings.h"
#include "lastfm_util.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/threadPool.h>

#include <atomic>
#include <string>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

static const GUID GUID_LASTFM_AUTHENTICATE = {
    0xb2f2b721, 0xdc90, 0x45ee, {0xa5, 0xb6, 0x46, 0x4d, 0xb5, 0x4f, 0x5d, 0x5f}};

static const GUID GUID_LASTFM_CLEAR_AUTH = {
    0x4b0a35e9, 0x9f8f, 0x4b3f, {0x9d, 0x0e, 0x2a, 0xbb, 0x47, 0xa4, 0x91, 0x73}};

static const GUID GUID_LASTFM_MENU_GROUP = {
    0x7f4f3aa1, 0x1b7c, 0x4b6e, {0x9a, 0x23, 0x4e, 0x8d, 0x17, 0x39, 0x52, 0x11}};

static const GUID GUID_LASTFM_SUSPEND = {0x3b5aca2b, 0x731e, 0x4ac4, {0xa3, 0xc5, 0x59, 0x4f, 0xcd, 0x27, 0xea, 0x49}};

namespace
{

static bool playbackMenuVisible()
{
    return lastfm::settings::showPlaybackMenu() || !lastfmIsAuthenticated();
}

class LastfmMenuGroup : public mainmenu_group_popup_v2
{
  public:
    GUID get_guid() override
    {
        return GUID_LASTFM_MENU_GROUP;
    }
    GUID get_parent() override
    {
        return mainmenu_groups::playback;
    }
    t_uint32 get_sort_priority() override
    {
        return mainmenu_commands::sort_priority_dontcare;
    }
    void get_display_string(pfc::string_base& out) override
    {
        out = "Last.fm";
    }
    bool popup_condition() override
    {
        return playbackMenuVisible();
    }
};

FB2K_SERVICE_FACTORY(LastfmMenuGroup);

static void openBrowserUrl(const std::string& url)
{
#if defined(__APPLE__)
    if (url.find('"') != std::string::npos)
        return;
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#elif defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    LFM_INFO("Open manually: (url omitted)");
#endif
}

static std::atomic<bool> authRequestInFlight{false};

static void runAuthenticateFlow()
{
    auto& authenticator = LastfmCore::instance().authenticator();

    std::string url;

    if (!authenticator.hasPendingToken())
    {
        const bool ok = authenticator.startAuth(url);
        if (ok && !url.empty())
        {
            popup_message::g_show("A browser window will open to authorize this foobar2000 instance with Last.fm.\n"
                                  "After allowing access, return here and click Authenticate again.",
                                  "Foo Scrobbler");
            openBrowserUrl(url);
        }
        else
        {
            authenticator.logout(); // Clear any half-started state
            popup_message::g_show("Failed to start authentication. Please try again.", "Foo Scrobbler");
        }
    }
    else
    {
        LastfmAuthState state;
        if (authenticator.completeAuth(state))
        {
            auto& core = LastfmCore::instance();

            // Prevent cross-account submission:
            const pfc::string8 owner = lastfmGetQueueOwnerUsername();
            const std::string newUser = state.username;

            if (owner.is_empty())
            {
                // First time: claim ownership.
                lastfmSetQueueOwnerUsername(newUser.c_str());
            }
            else if (std::string(owner.c_str()) != newUser)
            {
                // Different user: wipe pending scrobbles before draining.
                core.scrobbler().clearQueue();
                lastfmSetQueueOwnerUsername(newUser.c_str());
            }
            // else same user -> keep queue as-is

            lastfmSetAuthState(state);
            popup_message::g_show("Authentication complete.", "Foo Scrobbler");

            core.scrobbler().onAuthenticationRecovered();
            core.scrobbler().retryAsync();
        }
        else
        {
            // User likely closed browser or denied access. Reset and restart auth flow.
            authenticator.logout();

            std::string url2;
            if (authenticator.startAuth(url2) && !url2.empty())
            {
                popup_message::g_show("Authorization was not completed. Let's try again.\n"
                                      "A browser window will open to authorize this foobar2000 instance with Last.fm.\n"
                                      "After allowing access, return here and click Authenticate again.",
                                      "Foo Scrobbler");
                openBrowserUrl(url2);
            }
            else
            {
                popup_message::g_show("Authentication failed. Please try again.", "Foo Scrobbler");
            }
        }
    }
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
        out = lastfmIsSuspended() ? "Resume scrobbling" : "Pause scrobbling";
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
    if (!playbackMenuVisible())
        return false;

    const bool authed = lastfmIsAuthenticated();

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
        if (lastfmIsAuthenticated())
            return;

        if (authRequestInFlight.exchange(true))
        {
            LFM_DEBUG("Authentication request already in progress.");
            return;
        }

        fb2k::inWorkerThread(
            []
            {
                runAuthenticateFlow();
                authRequestInFlight.store(false);
            });
        break;
    }

    case CMD_CLEAR_AUTH:
    {
        auto& core = LastfmCore::instance();

        // Do NOT clear the queue here anymore.
        // Do NOT clear queue-owner either.
        core.scrobbler().resetInvalidSessionHandling();
        core.authenticator().logout();

        popup_message::g_show("Stored Last.fm authentication has been cleared.\n"
                              "Pending scrobbles are kept for this user.",
                              "Foo Scrobbler");
        break;
    }

    case CMD_SUSPEND:
    {
        if (lastfmIsSuspended())
        {
            lastfmClearSuspension();

            // The tracker notices the resume on its next playback tick and re-sends
            // Now Playing for the current track (NP-only path).

            // Do NOT retryAsync() here.
            // Queue will be retried on natural boundaries (next track -> onNowPlaying, stop -> retryAsync, etc).
        }
        else
        {
            lastfmSuspendCurrentUser();
        }
        break;
    }

    default:
        uBugCheck();
    }
}

static mainmenu_commands_factory_t<LastfmMenu> lastfmMenuFactory;
