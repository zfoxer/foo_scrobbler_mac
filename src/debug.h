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

void lastfmSetLogLevelFromConsoleChoice(int choice);
void lastfmSyncLogLevelFromPrefs();

#define LFM_INFO(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (lfmLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(LfmLogLevel::INFO))                        \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac: " << expr;                                                                    \
        }                                                                                                              \
    } while (0)

#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (lfmLogLevel.load(std::memory_order_relaxed) >= static_cast<int>(LfmLogLevel::DEBUG_LOG))                   \
        {                                                                                                              \
            console::formatter f;                                                                                      \
            f << "foo_scrobbler_mac: " << expr;                                                                        \
        }                                                                                                              \
    } while (0)
