//
//  debug.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <atomic>
#include <foobar2000/SDK/foobar2000.h>

enum class LFMLogLevel : int
{
    Off = 0,
    Info = 1,
    Debug = 2
};

// Global log level (runtime adjustable)
extern std::atomic<int> g_lfm_log_level;

// Internal helper
inline bool lfm_should_log_info()
{
    return g_lfm_log_level.load() >= (int)LFMLogLevel::Info;
}

inline bool lfm_should_log_debug()
{
    return g_lfm_log_level.load() >= (int)LFMLogLevel::Debug;
}

// Public macros
#define LFM_INFO(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (lfm_should_log_info())                                                                                     \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac: " << expr;                                                                    \
        }                                                                                                              \
    } while (0)

#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (lfm_should_log_debug())                                                                                    \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac [DEBUG]: " << expr;                                                            \
        }                                                                                                              \
    } while (0)
