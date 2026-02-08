//
//  debug.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>

#include <foobar2000/SDK/foobar2000.h>

enum class LfmLogLevel : int
{
    OFF = 0,
    INFO = 1,
    DEBUG_LOG = 2
};

extern std::atomic<int> lfmLogLevel;

// Defined in lastfm_prefs_pane.cpp
void lastfmSyncLogLevelFromPrefs();

#define LFM_INFO(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        lastfmSyncLogLevelFromPrefs();                                                                                 \
        if (lfmLogLevel.load() >= static_cast<int>(LfmLogLevel::INFO))                                                 \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac: " << expr;                                                                    \
        }                                                                                                              \
    } while (0)

#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        lastfmSyncLogLevelFromPrefs();                                                                                 \
        if (lfmLogLevel.load() >= static_cast<int>(LfmLogLevel::DEBUG_LOG))                                            \
        {                                                                                                              \
            console::formatter f;                                                                                      \
            f << "foo_scrobbler_mac: " << expr;                                                                \
        }                                                                                                              \
    } while (0)
