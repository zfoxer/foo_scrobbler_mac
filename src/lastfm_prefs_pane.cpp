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

static const GUID GUID_LASTFM_PREFS_BRANCH_CONSOLE = {
    0x19302d7d, 0x38b3, 0x4e75, {0xaa, 0xe1, 0x21, 0x32, 0xac, 0x99, 0x16, 0xbe}};

static const GUID GUID_LASTFM_PREFS_BRANCH_SCROBBLING = {
    0xebb7cd61, 0x95f3, 0x4b40, {0x9a, 0x7b, 0x9b, 0x54, 0xa6, 0xb3, 0x11, 0x49}};

// Branches
static advconfig_branch_factory g_lastfmPrefsBranchFactory("Foo Scrobbler", GUID_LASTFM_PREFS_BRANCH,
                                                           advconfig_branch::guid_branch_tools, -50);

static advconfig_branch_factory g_lastfmPrefsConsoleBranchFactory("Console info", GUID_LASTFM_PREFS_BRANCH_CONSOLE,
                                                                  GUID_LASTFM_PREFS_BRANCH, 0);

static advconfig_branch_factory g_lastfmPrefsScrobblingBranchFactory("Scrobbling", GUID_LASTFM_PREFS_BRANCH_SCROBBLING,
                                                                     GUID_LASTFM_PREFS_BRANCH, 1);

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

static void ensureRadioDefaultAdv()
{
    const bool r0 = advGetCheckboxState(GUID_LASTFM_PREFS_RADIO_0);
    const bool r1 = advGetCheckboxState(GUID_LASTFM_PREFS_RADIO_1);
    const bool r2 = advGetCheckboxState(GUID_LASTFM_PREFS_RADIO_2);

    if (r0 || r1 || r2)
        return;

    // Default = Basic
    advSetCheckboxState(GUID_LASTFM_PREFS_RADIO_1, true);
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
