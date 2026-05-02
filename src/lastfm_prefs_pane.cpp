//
//  lastfm_prefs_pane.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include <foobar2000/SDK/advconfig.h>
#include <foobar2000/SDK/advconfig_impl.h>
#include <foobar2000/SDK/foobar2000.h>

#include "lastfm_prefs_pane.h"
#include "debug.h"

#include <atomic>

namespace
{

// Preferences → Advanced → Tools → Foo Scrobbler
static const GUID GUID_LASTFM_PREFS_BRANCH = {
    0xb0d28e79, 0xead8, 0x44ac, {0x92, 0x77, 0x88, 0xfb, 0x3e, 0x91, 0x7d, 0x08}};

static const GUID GUID_LASTFM_PREFS_RADIO_0 = {
    0x00d152aa, 0x326b, 0x47fe, {0xad, 0x33, 0xfd, 0xb8, 0x4b, 0xc0, 0xd5, 0x02}};

static const GUID GUID_LASTFM_PREFS_RADIO_1 = {
    0x1e132773, 0x945c, 0x436f, {0x83, 0x53, 0x42, 0xb4, 0x5f, 0xc0, 0x56, 0xec}};

static const GUID GUID_LASTFM_PREFS_RADIO_2 = {
    0x37f7daf0, 0xc8af, 0x4977, {0x81, 0xd1, 0xa1, 0x44, 0x03, 0x37, 0x13, 0x6f}};

static const GUID GUID_LASTFM_PREFS_CHECKBOX_0 = {
    0x290f410b, 0x7c8d, 0x45b7, {0x8b, 0x2e, 0x8d, 0xd4, 0xf9, 0x41, 0x5a, 0x82}};

static const GUID GUID_LASTFM_PREFS_CHECKBOX_1 = {
    0xd9de934c, 0xf066, 0x4926, {0x82, 0xa0, 0x60, 0x19, 0x95, 0xe8, 0x16, 0x36}};

static const GUID GUID_LASTFM_PREFS_DYNAMIC_RADIO_0 = {
    0x38f2fe8f, 0xa3e5, 0x4933, {0x94, 0x1c, 0x84, 0x44, 0xc0, 0x9e, 0x09, 0x4a}}; // No dynamic sources

static const GUID GUID_LASTFM_PREFS_DYNAMIC_RADIO_1 = {
    0x31925388, 0x7774, 0x4365, {0xac, 0x67, 0x8e, 0x8d, 0x85, 0x14, 0x5d, 0x6a}}; // NP only

static const GUID GUID_LASTFM_PREFS_DYNAMIC_RADIO_2 = {
    0x6ff22bed, 0x84a5, 0x4f40, {0x89, 0x96, 0x87, 0x4a, 0x58, 0x54, 0x0b, 0x3b}}; // NP & Scrobbling (default)

static const GUID GUID_LASTFM_PREFS_BRANCH_CONSOLE = {
    0x19302d7d, 0x38b3, 0x4e75, {0xaa, 0xe1, 0x21, 0x32, 0xac, 0x99, 0x16, 0xbe}};

static const GUID GUID_LASTFM_PREFS_BRANCH_SCROBBLING = {
    0xebb7cd61, 0x95f3, 0x4b40, {0x9a, 0x7b, 0x9b, 0x54, 0xa6, 0xb3, 0x11, 0x49}};

static const GUID GUID_LASTFM_PREFS_BRANCH_DYNAMIC = {
    0xe370b8f5, 0xd383, 0x451d, {0x88, 0x58, 0x27, 0x62, 0x63, 0x4f, 0x34, 0xfd}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING = {
    0x3d1228c3, 0x3149, 0x46d9, {0xb7, 0x2d, 0x17, 0xd6, 0xe1, 0x17, 0xdd, 0x3c}};

static const GUID GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY = {
    0xbf8bb106, 0xf505, 0x406b, {0x8f, 0x58, 0x0e, 0xad, 0xec, 0xad, 0xb1, 0x71}};

static const GUID GUID_LASTFM_PREFS_EXCLUDE_ARTISTS = {
    0x1657f061, 0x825c, 0x4b22, {0xb6, 0x69, 0xdc, 0x72, 0xcb, 0xfd, 0x93, 0xe1}};

static const GUID GUID_LASTFM_PREFS_EXCLUDE_TITLES = {
    0xf168a4ff, 0xeb5b, 0x4e4c, {0xa5, 0x02, 0x65, 0x91, 0x08, 0x37, 0xdf, 0x0a}};

static const GUID GUID_LASTFM_PREFS_EXCLUDE_ALBUMS = {
    0x30f52005, 0x8cb8, 0x4358, {0x9d, 0xd0, 0x82, 0x87, 0xc8, 0x25, 0x26, 0x63}};

static const GUID GUID_LASTFM_PREFS_TF_ARTIST = {
    0x9a60376f, 0xc792, 0x4af9, {0xa1, 0x39, 0xa2, 0x0a, 0xd7, 0xba, 0x03, 0x6c}};

static const GUID GUID_LASTFM_PREFS_TF_ALBUM_ARTIST = {
    0x64317a5e, 0xba10, 0x4145, {0x81, 0x38, 0x5c, 0x00, 0xc7, 0xbc, 0x7e, 0xf8}};

static const GUID GUID_LASTFM_PREFS_TF_TITLE = {
    0xf7453915, 0x174c, 0x4356, {0x91, 0x85, 0x78, 0xbf, 0x14, 0xbc, 0x51, 0xbe}};

static const GUID GUID_LASTFM_PREFS_TF_ALBUM = {
    0xf9ed3ea2, 0x2dc3, 0x4116, {0x94, 0x58, 0xec, 0xf0, 0xbe, 0x05, 0x70, 0x45}};

// Branches
static advconfig_branch_factory g_lastfmPrefsBranchFactory("Foo Scrobbler", GUID_LASTFM_PREFS_BRANCH,
                                                           advconfig_branch::guid_branch_tools, -50);

static advconfig_branch_factory g_lastfmPrefsConsoleBranchFactory("Console info", GUID_LASTFM_PREFS_BRANCH_CONSOLE,
                                                                  GUID_LASTFM_PREFS_BRANCH, 0);

static advconfig_branch_factory g_lastfmPrefsTagFormattingBranchFactory("Tag formatting",
                                                                        GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING,
                                                                        GUID_LASTFM_PREFS_BRANCH, 1);

static advconfig_branch_factory g_lastfmPrefsScrobblingBranchFactory("Scrobbling", GUID_LASTFM_PREFS_BRANCH_SCROBBLING,
                                                                     GUID_LASTFM_PREFS_BRANCH, 2);

static advconfig_branch_factory g_lastfmPrefsDynamicBranchFactory("Dynamic sources (overridden by library-only)",
                                                                  GUID_LASTFM_PREFS_BRANCH_DYNAMIC,
                                                                  GUID_LASTFM_PREFS_BRANCH, 3);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagCheckboxTreatVA("Treat \"Various Artists\" as empty (Album Artist only)",
                         "foo_scrobbler.tags.compilation.treat_va_empty", GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY,
                         GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING, 4.0, false, false, 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_tagArtistTf("Artist (Title Formatting)", "foo_scrobbler.tf.artist", GUID_LASTFM_PREFS_TF_ARTIST,
                  GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING, 0.0, "[%ARTIST%]", 0);

static service_factory_single_t<advconfig_entry_string_impl> g_tagAlbumArtistTf("Album Artist (Title Formatting)",
                                                                                "foo_scrobbler.tf.album_artist",
                                                                                GUID_LASTFM_PREFS_TF_ALBUM_ARTIST,
                                                                                GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING,
                                                                                1.0, "[%ALBUM ARTIST%]", 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_tagTitleTf("Title (Title Formatting)", "foo_scrobbler.tf.title", GUID_LASTFM_PREFS_TF_TITLE,
                 GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING, 2.0, "[%TITLE%]", 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_tagAlbumTf("Album (Title Formatting)", "foo_scrobbler.tf.album", GUID_LASTFM_PREFS_TF_ALBUM,
                 GUID_LASTFM_PREFS_BRANCH_TAG_FORMATTING, 3.0, "[%ALBUM%]", 0);

static bool advGetCheckboxState(const GUID& g)
{
    service_ptr_t<advconfig_entry_checkbox> e;
    if (!advconfig_entry::g_find_t(e, g))
        return false;
    return e->get_state();
}

static void advSetCheckboxState(const GUID& g, bool v)
{
    service_ptr_t<advconfig_entry_checkbox> e;
    if (!advconfig_entry::g_find_t(e, g))
        return;
    e->set_state(v);
}

// Radios
static service_factory_single_t<advconfig_entry_checkbox_impl> g_radio0("None", "foo_scrobbler.console.no",
                                                                        GUID_LASTFM_PREFS_RADIO_0,
                                                                        GUID_LASTFM_PREFS_BRANCH_CONSOLE, 0.0, false,
                                                                        true, // isRadio
                                                                        0     // flags
);

static service_factory_single_t<advconfig_entry_checkbox_impl> g_radio1("Basic", "foo_scrobbler.console.basic",
                                                                        GUID_LASTFM_PREFS_RADIO_1,
                                                                        GUID_LASTFM_PREFS_BRANCH_CONSOLE, 1.0, true,
                                                                        true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl> g_radio2("Debug", "foo_scrobbler.console.debug",
                                                                        GUID_LASTFM_PREFS_RADIO_2,
                                                                        GUID_LASTFM_PREFS_BRANCH_CONSOLE, 2.0, false,
                                                                        true, 0);

// Checkboxes, defaults: no, no
static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_checkbox0("Disable NowPlaying notifications", "foo_scrobbler.scrobbling.disable_nowplaying",
                GUID_LASTFM_PREFS_CHECKBOX_0, GUID_LASTFM_PREFS_BRANCH_SCROBBLING, 0.0, false, false, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_checkbox1("Only scrobble from media library", "foo_scrobbler.scrobbling.only_from_library",
                GUID_LASTFM_PREFS_CHECKBOX_1, GUID_LASTFM_PREFS_BRANCH_SCROBBLING, 1.0, false, false, 0);

// Dynamic sources (3-choice radio group)
static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_dynamicRadio0("No dynamic sources", "foo_scrobbler.dynamic.no", GUID_LASTFM_PREFS_DYNAMIC_RADIO_0,
                    GUID_LASTFM_PREFS_BRANCH_DYNAMIC, 0.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_dynamicRadio1("Only NP notifications", "foo_scrobbler.dynamic.np_only", GUID_LASTFM_PREFS_DYNAMIC_RADIO_1,
                    GUID_LASTFM_PREFS_BRANCH_DYNAMIC, 1.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_dynamicRadio2("NP & Scrobbling", "foo_scrobbler.dynamic.np_and_scrobble", GUID_LASTFM_PREFS_DYNAMIC_RADIO_2,
                    GUID_LASTFM_PREFS_BRANCH_DYNAMIC, 2.0, true, true, 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_excludeArtists("Exclude artists (text or regex; ';' separated)", "foo_scrobbler.scrobbling.exclude_artists",
                     GUID_LASTFM_PREFS_EXCLUDE_ARTISTS, GUID_LASTFM_PREFS_BRANCH_SCROBBLING, 2.0, "", 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_excludeTitles("Exclude titles (text or regex; ';' separated)", "foo_scrobbler.scrobbling.exclude_titles",
                    GUID_LASTFM_PREFS_EXCLUDE_TITLES, GUID_LASTFM_PREFS_BRANCH_SCROBBLING, 3.0, "", 0);

static service_factory_single_t<advconfig_entry_string_impl>
    g_excludeAlbums("Exclude albums (text or regex; ';' separated)", "foo_scrobbler.scrobbling.exclude_albums",
                    GUID_LASTFM_PREFS_EXCLUDE_ALBUMS, GUID_LASTFM_PREFS_BRANCH_SCROBBLING, 4.0, "", 0);

static void enforceOneOfN(const GUID* ids, std::size_t n, std::size_t defaultIndex)
{
    std::size_t firstOn = n; // "none"
    std::size_t onCount = 0;

    for (std::size_t i = 0; i < n; ++i)
    {
        if (advGetCheckboxState(ids[i]))
        {
            if (firstOn == n)
                firstOn = i;
            ++onCount;
        }
    }

    // If none ON -> select default.
    if (onCount == 0)
    {
        for (std::size_t i = 0; i < n; ++i)
            advSetCheckboxState(ids[i], i == defaultIndex);
        return;
    }

    // If multiple ON -> keep the first one we saw, clear the rest.
    if (onCount > 1)
    {
        for (std::size_t i = 0; i < n; ++i)
            advSetCheckboxState(ids[i], i == firstOn);
    }
}

static void enforceOneOf3(const GUID& g0, const GUID& g1, const GUID& g2, std::size_t defaultIndex)
{
    const GUID ids[3] = {g0, g1, g2};
    enforceOneOfN(ids, 3, defaultIndex);
}

static void ensureRadioDefaultAdv()
{
    // Default = Basic (index 1)
    enforceOneOf3(GUID_LASTFM_PREFS_RADIO_0, GUID_LASTFM_PREFS_RADIO_1, GUID_LASTFM_PREFS_RADIO_2, 1);
}

static int getConsoleRadioChoice()
{
    ensureRadioDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_PREFS_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_PREFS_RADIO_1))
        return 1;
    return 0;
}

static void ensureDynamicRadioDefaultAdv()
{
    // Default = NP & Scrobbling (index 2)
    enforceOneOf3(GUID_LASTFM_PREFS_DYNAMIC_RADIO_0, GUID_LASTFM_PREFS_DYNAMIC_RADIO_1,
                  GUID_LASTFM_PREFS_DYNAMIC_RADIO_2, 2);
}

static int getDynamicSourcesMode()
{
    ensureDynamicRadioDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_PREFS_CHECKBOX_1))
    {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LFM_DEBUG("Dynamic sources: overridden to 'No dynamic sources' because Only-from-library is enabled.");
        return 0;
    }

    if (advGetCheckboxState(GUID_LASTFM_PREFS_DYNAMIC_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_PREFS_DYNAMIC_RADIO_1))
        return 1;
    return 0;
}

static std::string advGetStringState(const GUID& g)
{
    service_ptr_t<advconfig_entry_string> e;
    if (!advconfig_entry::g_find_t(e, g))
        return {};

    pfc::string8 v;
    e->get_state(v);
    return std::string(v.c_str());
}
} // namespace

void lastfmSyncLogLevelFromPrefs()
{
    const int choice = getConsoleRadioChoice();

    const int desired = (choice == 0)   ? static_cast<int>(LfmLogLevel::OFF)
                        : (choice == 1) ? static_cast<int>(LfmLogLevel::INFO)
                                        : static_cast<int>(LfmLogLevel::DEBUG_LOG);

    lfmLogLevel.store(desired);
}

void lastfmRegisterPrefsPane()
{
    // Force sync once at startup, so atomic matches prefs immediately.
    lastfmSyncLogLevelFromPrefs();

    pfc::string_formatter f;
    f << "PrefsPane: Advanced prefs registered. consoleChoice=" << getConsoleRadioChoice()
      << " logLevel=" << lfmLogLevel.load();
    LFM_DEBUG(f.c_str());
}

bool lastfmOnlyScrobbleFromMediaLibrary()
{
    return advGetCheckboxState(GUID_LASTFM_PREFS_CHECKBOX_1);
}

int lastfmDynamicSourcesMode()
{
    return getDynamicSourcesMode();
}

bool lastfmDisableNowPlaying()
{
    return advGetCheckboxState(GUID_LASTFM_PREFS_CHECKBOX_0);
}

std::string lastfmExcludedArtistsPatternList()
{
    return advGetStringState(GUID_LASTFM_PREFS_EXCLUDE_ARTISTS);
}

std::string lastfmExcludedTitlesPatternList()
{
    return advGetStringState(GUID_LASTFM_PREFS_EXCLUDE_TITLES);
}

std::string lastfmExcludedAlbumsPatternList()
{
    return advGetStringState(GUID_LASTFM_PREFS_EXCLUDE_ALBUMS);
}

std::string lastfmArtistTf()
{
    return advGetStringState(GUID_LASTFM_PREFS_TF_ARTIST);
}

std::string lastfmAlbumArtistTf()
{
    return advGetStringState(GUID_LASTFM_PREFS_TF_ALBUM_ARTIST);
}

std::string lastfmTitleTf()
{
    return advGetStringState(GUID_LASTFM_PREFS_TF_TITLE);
}

std::string lastfmAlbumTf()
{
    return advGetStringState(GUID_LASTFM_PREFS_TF_ALBUM);
}

bool lastfmTagTreatVariousArtistsAsEmpty()
{
    return advGetCheckboxState(GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY);
}
