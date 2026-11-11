//
//  lastfm_menu.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_menu.h"
#include "lastfm_state.h"
#include "lastfm_settings.h"

#include <foobar2000/SDK/foobar2000.h>

static const GUID GUID_LASTFM_MENU_GROUP = {
    0x7f4f3aa1, 0x1b7c, 0x4b6e, {0x9a, 0x23, 0x4e, 0x8d, 0x17, 0x39, 0x52, 0x11}};

static const GUID GUID_LASTFM_SUSPEND = {0x3b5aca2b, 0x731e, 0x4ac4, {0xa3, 0xc5, 0x59, 0x4f, 0xcd, 0x27, 0xea, 0x49}};

namespace
{

static bool playbackMenuVisible()
{
    return lastfm::settings::showPlaybackMenu() && lastfmIsAuthenticated();
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
} // namespace

t_uint32 LastfmMenu::get_command_count()
{
    return CMD_COUNT;
}

GUID LastfmMenu::get_command(t_uint32 index)
{
    switch (index)
    {
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
    if (index != CMD_SUSPEND || !playbackMenuVisible())
        return false;

    get_name(index, text);
    return true;
}

void LastfmMenu::execute(t_uint32 index, ctx_t)
{
    switch (index)
    {
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
