//
//  lastfm_menu.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_menu.h"
#include "lastfm_ui.h"
#include "lastfm_core.h"
#include "lastfm_track_info.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <string>
#include <cstdlib>
#include <cctype>

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

static void openBrowserUrl(const std::string& url)
{
#if defined(__APPLE__)
    if (url.find('"') != std::string::npos)
        return;
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#else
    LFM_INFO("Open manually: (url omitted)");
#endif
}

static std::string cleanTagValue(const char* value)
{
    if (!value)
        return {};

    std::string s(value);

    std::size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;

    std::size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1]))
        --end;

    if (start == end)
        return {};

    s = s.substr(start, end - start);

    std::string norm;
    for (char c : s)
        if (!std::isspace((unsigned char)c))
            norm.push_back((char)std::tolower((unsigned char)c));

    if (norm == "unknown" || norm == "unknownartist" || norm == "unknowntrack")
        return {};

    return s;
}

static bool getNowPlayingTrackInfo(LastfmTrackInfo& out)
{
    out = LastfmTrackInfo{};

    metadb_handle_ptr handle;
    if (!playback_control::get()->get_now_playing(handle) || !handle.is_valid())
        return false;

    file_info_impl info;
    if (!handle->get_info(info))
        return false;

    out.artist = cleanTagValue(info.meta_get("artist", 0));
    out.title = cleanTagValue(info.meta_get("title", 0));
    out.album = cleanTagValue(info.meta_get("album", 0));

    const char* mbid = info.meta_get("musicbrainz_trackid", 0);
    out.mbid = mbid ? mbid : "";

    out.durationSeconds = info.get_length();

    // For Now Playing, if artist/title are missing, just skip sending.
    return !out.artist.empty() && !out.title.empty();
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
    auto& authenticator = LastfmCore::instance().authenticator();
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
    {
        auto& core = LastfmCore::instance();
        core.scrobbler().clearQueue();
        core.scrobbler().resetInvalidSessionHandling();
        core.authenticator().logout();

        popup_message::g_show("Stored Last.fm authentication has been cleared.", "Last.fm");
        break;
    }

    case CMD_SUSPEND:
    {
        auto& core = LastfmCore::instance();

        if (isSuspended())
        {
            clearSuspension();

            // Send Now Playing immediately for the currently playing track.
            // Use sendNowPlayingOnly() so we do NOT flush the retry queue on resume.
            LastfmTrackInfo now;
            if (getNowPlayingTrackInfo(now))
                core.scrobbler().sendNowPlayingOnly(now);

            // Do NOT retryAsync() here.
            // Queue will be retried on natural boundaries (next track -> onNowPlaying, stop -> retryAsync, etc).
        }
        else
        {
            suspendCurrentUser();
        }
        break;
    }

    default:
        uBugCheck();
    }
}

static mainmenu_commands_factory_t<LastfmMenu> lastfmMenuFactory;
