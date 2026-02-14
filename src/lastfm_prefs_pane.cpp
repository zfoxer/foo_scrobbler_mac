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

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING = {
    0xc79e187c, 0xc335, 0x41c5, {0xa7, 0x67, 0x53, 0x55, 0xc0, 0x90, 0xcb, 0x03}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST = {
    0xbe32efc8, 0xe8d6, 0x4a75, {0xab, 0x08, 0x27, 0xc1, 0xcd, 0x72, 0xc7, 0x27}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST = {
    0x311ad8ba, 0x1b74, 0x417c, {0xb4, 0x20, 0xec, 0x77, 0xef, 0x2b, 0x15, 0xad}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_TITLE = {
    0x2b591194, 0x5ce2, 0x454d, {0x86, 0x96, 0x9d, 0xaf, 0x05, 0x33, 0x1f, 0xb5}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM = {
    0xcd795ea5, 0x96f3, 0x4f7b, {0xad, 0xae, 0xd6, 0xd9, 0xc2, 0xa6, 0xd5, 0xa1}};

static const GUID GUID_LASTFM_PREFS_BRANCH_TAG_FALLBACK_COMP = {
    0xa290de48, 0x3f79, 0x41df, {0x96, 0x77, 0xd7, 0x3c, 0xeb, 0x09, 0xe7, 0x6b}};

// Artist source radios (5-choice group)
static const GUID GUID_LASTFM_TAG_ARTIST_RADIO_0 = {
    0x77bd0932, 0xea2c, 0x47ba, {0xad, 0x73, 0x34, 0x6d, 0x07, 0xd9, 0xc9, 0x79} /* ARTIST */};

static const GUID GUID_LASTFM_TAG_ARTIST_RADIO_1 = {
    0x3af5bbf7, 0x5450, 0x4769, {0x84, 0x3b, 0xfa, 0x98, 0x96, 0xfa, 0x94, 0x62} /* ALBUM ARTIST */};

static const GUID GUID_LASTFM_TAG_ARTIST_RADIO_2 = {
    0x0751e774, 0xc428, 0x4354, {0x98, 0x58, 0x93, 0x40, 0xbb, 0x16, 0x78, 0x06} /* PERFORMER */};

static const GUID GUID_LASTFM_TAG_ARTIST_RADIO_3 = {
    0x1ec91290, 0x7e98, 0x412b, {0xb3, 0x4b, 0x0e, 0x86, 0x5e, 0x6c, 0x83, 0x45} /* COMPOSER */};

static const GUID GUID_LASTFM_TAG_ARTIST_RADIO_4 = {
    0x2b239a2b, 0x83bc, 0x4a20, {0xb6, 0x46, 0x1c, 0x84, 0xe9, 0xe4, 0x69, 0xd0} /* CONDUCTOR */};

static const GUID GUID_LASTFM_TAG_CHECKBOX_FALLBACK_ARTIST_ALBUM = {
    0x76c075ec, 0xb937, 0x406c, {0x9c, 0x29, 0x0f, 0xd0, 0xbc, 0x2e, 0x71, 0x85}};

static const GUID GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY = {
    0xbf8bb106, 0xf505, 0x406b, {0x8f, 0x58, 0x0e, 0xad, 0xec, 0xad, 0xb1, 0x71}};

static const GUID GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_0 = {
    /* ALBUM ARTIST (default) */ 0xbf01f572, 0x9400, 0x4ffd, {0x80, 0x09, 0x9e, 0x27, 0x1d, 0x71, 0x42, 0x4f}};

static const GUID GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_1 = {
    /* ARTIST */ 0xfad25669, 0x6347, 0x4086, {0x99, 0x93, 0xf5, 0x72, 0xb1, 0xb1, 0xd6, 0xae}};

static const GUID GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_2 = {
    /* PERFORMER */ 0xff75c9cc, 0x3e67, 0x4456, {0xa7, 0xd0, 0xfd, 0xd7, 0xe4, 0x4f, 0xbc, 0x59}};

static const GUID GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_3 = {
    /* COMPOSER */ 0xec829e11, 0x2805, 0x47c6, {0x94, 0x64, 0x1f, 0xc7, 0x77, 0xbe, 0xde, 0x7d}};

static const GUID GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_4 = {
    /* CONDUCTOR */ 0x7d770e4e, 0xebf8, 0x4bf4, {0xa1, 0x05, 0x39, 0xaf, 0x53, 0x1b, 0x30, 0xee}};

static const GUID GUID_LASTFM_TAG_TITLE_RADIO_0 = {
    /* TITLE (default) */ 0x3c799e00, 0x9bc8, 0x48c8, {0xbc, 0x33, 0x16, 0x22, 0x52, 0x77, 0xa2, 0x11}};

static const GUID GUID_LASTFM_TAG_TITLE_RADIO_1 = {
    /* WORK */ 0x82b73885, 0x8d75, 0x4098, {0x93, 0xac, 0xad, 0x57, 0x4f, 0xc1, 0xc7, 0x67}};

static const GUID GUID_LASTFM_TAG_TITLE_RADIO_2 = {
    /* MOVEMENT */ 0x3590bc13, 0xe8af, 0x4433, {0xa2, 0xe1, 0xcb, 0x2a, 0x44, 0x92, 0xf2, 0x42}};

static const GUID GUID_LASTFM_TAG_TITLE_RADIO_3 = {
    /* PART */ 0x1bbe74d9, 0x34fa, 0x43b8, {0x88, 0x0a, 0xa6, 0x5e, 0xbb, 0x07, 0xc0, 0x9e}};

static const GUID GUID_LASTFM_TAG_TITLE_RADIO_4 = {
    /* SUBTITLE */ 0xac9d6801, 0x5ddd, 0x4a09, {0xb2, 0x85, 0x12, 0x15, 0x8d, 0x67, 0x09, 0xa2}};

static const GUID GUID_LASTFM_TAG_ALBUM_RADIO_0 = {
    /* ALBUM (default) */ 0xbae641d7, 0x265f, 0x434b, {0x90, 0xd6, 0x79, 0x06, 0xf2, 0x94, 0xd9, 0x9b}};

static const GUID GUID_LASTFM_TAG_ALBUM_RADIO_1 = {
    /* RELEASE */ 0xc2b1b88f, 0x7cda, 0x4844, {0x8c, 0x27, 0xf5, 0x2e, 0x33, 0xe9, 0xd9, 0x73}};

static const GUID GUID_LASTFM_TAG_ALBUM_RADIO_2 = {
    /* WORK */ 0x30894fb5, 0x38e8, 0x400c, {0xaf, 0xcf, 0xd6, 0x18, 0x58, 0x7c, 0x89, 0x8d}};

static const GUID GUID_LASTFM_TAG_ALBUM_RADIO_3 = {
    /* ALBUMTITLE */ 0x882915ed, 0xbd34, 0x4195, {0xb9, 0xaa, 0x1d, 0xa7, 0x26, 0x8f, 0xc2, 0x91}};

static const GUID GUID_LASTFM_TAG_ALBUM_RADIO_4 = {
    /* DISCNAME */ 0x50f68c18, 0x5f3c, 0x4ab6, {0x8a, 0x1e, 0x85, 0x7b, 0x1a, 0x2f, 0x92, 0x51}};

// Branches
static advconfig_branch_factory g_lastfmPrefsBranchFactory("Foo Scrobbler", GUID_LASTFM_PREFS_BRANCH,
                                                           advconfig_branch::guid_branch_tools, -50);

static advconfig_branch_factory g_lastfmPrefsConsoleBranchFactory("Console info", GUID_LASTFM_PREFS_BRANCH_CONSOLE,
                                                                  GUID_LASTFM_PREFS_BRANCH, 0);

static advconfig_branch_factory g_lastfmPrefsScrobblingBranchFactory("Scrobbling", GUID_LASTFM_PREFS_BRANCH_SCROBBLING,
                                                                     GUID_LASTFM_PREFS_BRANCH, 1);

static advconfig_branch_factory g_lastfmPrefsDynamicBranchFactory("Dynamic sources (overridden by library-only)",
                                                                  GUID_LASTFM_PREFS_BRANCH_DYNAMIC,
                                                                  GUID_LASTFM_PREFS_BRANCH, 2);

static advconfig_branch_factory g_lastfmPrefsTagHandlingBranchFactory("Tag Handling",
                                                                      GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING,
                                                                      GUID_LASTFM_PREFS_BRANCH,
                                                                      3 // after Dynamic sources
);

static advconfig_branch_factory g_tagArtistBranch("Artist source", GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST,
                                                  GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING, 0);

static advconfig_branch_factory g_tagAlbumArtistBranch("Album Artist source", GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST,
                                                       GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING, 1);

static advconfig_branch_factory g_tagTitleBranch("Title source", GUID_LASTFM_PREFS_BRANCH_TAG_TITLE,
                                                 GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING, 2);

static advconfig_branch_factory g_tagAlbumBranch("Album source", GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM,
                                                 GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING, 3);

static advconfig_branch_factory g_tagFallbackCompBranch("Fallback & Compilation Handling",
                                                        GUID_LASTFM_PREFS_BRANCH_TAG_FALLBACK_COMP,
                                                        GUID_LASTFM_PREFS_BRANCH_TAG_HANDLING, 4);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumArtistRadio0("ALBUM ARTIST", "foo_scrobbler.tags.album_artist.album_artist",
                           GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_0, GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST, 0.0, true,
                           true, 0); // default ON

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumArtistRadio1("ARTIST", "foo_scrobbler.tags.album_artist.artist", GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_1,
                           GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST, 1.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumArtistRadio2("PERFORMER", "foo_scrobbler.tags.album_artist.performer",
                           GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_2, GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST, 2.0, false,
                           true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumArtistRadio3("COMPOSER", "foo_scrobbler.tags.album_artist.composer", GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_3,
                           GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST, 3.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumArtistRadio4("CONDUCTOR", "foo_scrobbler.tags.album_artist.conductor",
                           GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_4, GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM_ARTIST, 4.0, false,
                           true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagCheckboxFallback("If selected Artist or Album source is empty, fall back to ARTIST / ALBUM",
                          "foo_scrobbler.tags.fallback.artist_album", GUID_LASTFM_TAG_CHECKBOX_FALLBACK_ARTIST_ALBUM,
                          GUID_LASTFM_PREFS_BRANCH_TAG_FALLBACK_COMP, 0.0, true, false, 0); // default ON

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagCheckboxTreatVA("Treat \"Various Artists\" as empty (Album Artist only)",
                         "foo_scrobbler.tags.compilation.treat_va_empty", GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY,
                         GUID_LASTFM_PREFS_BRANCH_TAG_FALLBACK_COMP, 1.0, false, false, 0); // default OFF

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagTitleRadio0("TITLE", "foo_scrobbler.tags.title.title", GUID_LASTFM_TAG_TITLE_RADIO_0,
                     GUID_LASTFM_PREFS_BRANCH_TAG_TITLE, 0.0, true, true, 0); // default ON

static service_factory_single_t<advconfig_entry_checkbox_impl> g_tagTitleRadio1("WORK", "foo_scrobbler.tags.title.work",
                                                                                GUID_LASTFM_TAG_TITLE_RADIO_1,
                                                                                GUID_LASTFM_PREFS_BRANCH_TAG_TITLE, 1.0,
                                                                                false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagTitleRadio2("MOVEMENT", "foo_scrobbler.tags.title.movement", GUID_LASTFM_TAG_TITLE_RADIO_2,
                     GUID_LASTFM_PREFS_BRANCH_TAG_TITLE, 2.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl> g_tagTitleRadio3("PART", "foo_scrobbler.tags.title.part",
                                                                                GUID_LASTFM_TAG_TITLE_RADIO_3,
                                                                                GUID_LASTFM_PREFS_BRANCH_TAG_TITLE, 3.0,
                                                                                false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagTitleRadio4("SUBTITLE", "foo_scrobbler.tags.title.subtitle", GUID_LASTFM_TAG_TITLE_RADIO_4,
                     GUID_LASTFM_PREFS_BRANCH_TAG_TITLE, 4.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumRadio0("ALBUM", "foo_scrobbler.tags.album.album", GUID_LASTFM_TAG_ALBUM_RADIO_0,
                     GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM, 0.0, true, true, 0); // default ON

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumRadio1("RELEASE", "foo_scrobbler.tags.album.release", GUID_LASTFM_TAG_ALBUM_RADIO_1,
                     GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM, 1.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl> g_tagAlbumRadio2("WORK", "foo_scrobbler.tags.album.work",
                                                                                GUID_LASTFM_TAG_ALBUM_RADIO_2,
                                                                                GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM, 2.0,
                                                                                false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumRadio3("ALBUMTITLE", "foo_scrobbler.tags.album.albumtitle", GUID_LASTFM_TAG_ALBUM_RADIO_3,
                     GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM, 3.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagAlbumRadio4("DISCNAME", "foo_scrobbler.tags.album.discname", GUID_LASTFM_TAG_ALBUM_RADIO_4,
                     GUID_LASTFM_PREFS_BRANCH_TAG_ALBUM, 4.0, false, true, 0);

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

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagArtistRadio0("ARTIST", "foo_scrobbler.tags.artist.artist", GUID_LASTFM_TAG_ARTIST_RADIO_0,
                      GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST, 0.0, true, true, 0); // default ON

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagArtistRadio1("ALBUM ARTIST", "foo_scrobbler.tags.artist.album_artist", GUID_LASTFM_TAG_ARTIST_RADIO_1,
                      GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST, 1.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagArtistRadio2("PERFORMER", "foo_scrobbler.tags.artist.performer", GUID_LASTFM_TAG_ARTIST_RADIO_2,
                      GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST, 2.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagArtistRadio3("COMPOSER", "foo_scrobbler.tags.artist.composer", GUID_LASTFM_TAG_ARTIST_RADIO_3,
                      GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST, 3.0, false, true, 0);

static service_factory_single_t<advconfig_entry_checkbox_impl>
    g_tagArtistRadio4("CONDUCTOR", "foo_scrobbler.tags.artist.conductor", GUID_LASTFM_TAG_ARTIST_RADIO_4,
                      GUID_LASTFM_PREFS_BRANCH_TAG_ARTIST, 4.0, false, true, 0);

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

static void enforceOneOf5(const GUID& g0, const GUID& g1, const GUID& g2, const GUID& g3, const GUID& g4,
                          std::size_t defaultIndex)
{
    const GUID ids[5] = {g0, g1, g2, g3, g4};
    enforceOneOfN(ids, 5, defaultIndex);
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

static void ensureTagArtistDefaultAdv()
{
    // Default = ARTIST (index 0)
    enforceOneOf5(GUID_LASTFM_TAG_ARTIST_RADIO_0, GUID_LASTFM_TAG_ARTIST_RADIO_1, GUID_LASTFM_TAG_ARTIST_RADIO_2,
                  GUID_LASTFM_TAG_ARTIST_RADIO_3, GUID_LASTFM_TAG_ARTIST_RADIO_4, 0);
}

static int getTagArtistSourceChoice()
{
    ensureTagArtistDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_TAG_ARTIST_RADIO_4))
        return 4;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ARTIST_RADIO_3))
        return 3;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ARTIST_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ARTIST_RADIO_1))
        return 1;
    return 0;
}

static void ensureTagAlbumArtistDefaultAdv()
{
    // Default = ALBUM ARTIST (index 0)
    enforceOneOf5(GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_0, GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_1,
                  GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_2, GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_3,
                  GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_4, 0);
}

static int getTagAlbumArtistSourceChoice()
{
    ensureTagAlbumArtistDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_4))
        return 4;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_3))
        return 3;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_ARTIST_RADIO_1))
        return 1;
    return 0;
}

static void ensureTagTitleDefaultAdv()
{
    // Default = TITLE (index 0)
    enforceOneOf5(GUID_LASTFM_TAG_TITLE_RADIO_0, GUID_LASTFM_TAG_TITLE_RADIO_1, GUID_LASTFM_TAG_TITLE_RADIO_2,
                  GUID_LASTFM_TAG_TITLE_RADIO_3, GUID_LASTFM_TAG_TITLE_RADIO_4, 0);
}

static int getTagTitleSourceChoice()
{
    ensureTagTitleDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_TAG_TITLE_RADIO_4))
        return 4;
    if (advGetCheckboxState(GUID_LASTFM_TAG_TITLE_RADIO_3))
        return 3;
    if (advGetCheckboxState(GUID_LASTFM_TAG_TITLE_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_TAG_TITLE_RADIO_1))
        return 1;
    return 0;
}

static void ensureTagAlbumDefaultAdv()
{
    // Default = ALBUM (index 0)
    enforceOneOf5(GUID_LASTFM_TAG_ALBUM_RADIO_0, GUID_LASTFM_TAG_ALBUM_RADIO_1, GUID_LASTFM_TAG_ALBUM_RADIO_2,
                  GUID_LASTFM_TAG_ALBUM_RADIO_3, GUID_LASTFM_TAG_ALBUM_RADIO_4, 0);
}

static int getTagAlbumSourceChoice()
{
    ensureTagAlbumDefaultAdv();

    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_RADIO_4))
        return 4;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_RADIO_3))
        return 3;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_RADIO_2))
        return 2;
    if (advGetCheckboxState(GUID_LASTFM_TAG_ALBUM_RADIO_1))
        return 1;
    return 0;
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

int lastfmTagArtistSource()
{
    return getTagArtistSourceChoice();
}

bool lastfmTagFallbackArtistAlbum()
{
    return advGetCheckboxState(GUID_LASTFM_TAG_CHECKBOX_FALLBACK_ARTIST_ALBUM);
}

bool lastfmTagTreatVariousArtistsAsEmpty()
{
    return advGetCheckboxState(GUID_LASTFM_TAG_CHECKBOX_VA_AS_EMPTY);
}

int lastfmTagAlbumArtistSource()
{
    return getTagAlbumArtistSourceChoice();
}

int lastfmTagTitleSource()
{
    return getTagTitleSourceChoice();
}

int lastfmTagAlbumSource()
{
    return getTagAlbumSourceChoice();
}
