//
//  lastfm_prefs_pane_state.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_prefs_pane_state.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <mutex>

namespace
{
static const GUID GUID_CFG_LASTFM_PREFS_PANE_RADIO = {
    0xefb846f6, 0x3487, 0x4d01, {0xba, 0x6b, 0xa9, 0x50, 0xd2, 0xeb, 0x69, 0x05}};

static const GUID GUID_CFG_LASTFM_PREFS_PANE_CHECKBOX = {
    0x55a9ec1f, 0x9882, 0x48b1, {0x91, 0x86, 0x98, 0x79, 0xb8, 0x23, 0x7d, 0x7c}};

// Defaults: radio = middle option, checkbox = off
static cfg_int cfgLastfmPrefsPaneRadio(GUID_CFG_LASTFM_PREFS_PANE_RADIO, 1);
static cfg_bool cfgLastfmPrefsPaneCheckbox(GUID_CFG_LASTFM_PREFS_PANE_CHECKBOX, false);

static std::mutex prefsPaneMutex;

static int clampRadioChoice(int v)
{
    if (v < 0)
        return 0;
    if (v > 2)
        return 2;
    return v;
}
} // namespace

int lastfmGetPrefsPaneRadioChoice()
{
    std::lock_guard<std::mutex> lock(prefsPaneMutex);
    const t_int64 raw = cfgLastfmPrefsPaneRadio.get();
    return clampRadioChoice(static_cast<int>(raw));
}

void lastfmSetPrefsPaneRadioChoice(int value)
{
    value = clampRadioChoice(value);

    std::lock_guard<std::mutex> lock(prefsPaneMutex);
    cfgLastfmPrefsPaneRadio.set(value);
    LFM_DEBUG("PrefsPane: set radio choice.");
}

bool lastfmGetPrefsPaneCheckbox()
{
    std::lock_guard<std::mutex> lock(prefsPaneMutex);
    return cfgLastfmPrefsPaneCheckbox.get();
}

void lastfmSetPrefsPaneCheckbox(bool enabled)
{
    std::lock_guard<std::mutex> lock(prefsPaneMutex);
    cfgLastfmPrefsPaneCheckbox.set(enabled);
    LFM_DEBUG("PrefsPane: set checkbox.");
}
