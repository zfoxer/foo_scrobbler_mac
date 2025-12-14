//
//  debug.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
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

// Global log level (runtime adjustable)
extern std::atomic<int> lfmLogLevel;

inline bool shouldLogInfo()
{
    return lfmLogLevel.load() >= static_cast<int>(LfmLogLevel::INFO);
}

inline bool shouldLogDebug()
{
    return lfmLogLevel.load() >= static_cast<int>(LfmLogLevel::DEBUG_LOG);
}

#define LFM_INFO(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (shouldLogInfo())                                                                                           \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac: " << expr;                                                                    \
        }                                                                                                              \
    } while (0)

#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (shouldLogDebug())                                                                                          \
        {                                                                                                              \
            console::formatter lfm_f;                                                                                  \
            lfm_f << "foo_scrobbler_mac [DEBUG]: " << expr;                                                            \
        }                                                                                                              \
    } while (0)
