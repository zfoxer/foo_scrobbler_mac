//
//  debug.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#ifdef DEBUG
#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        console::formatter f;                                                                                          \
        f << "foo_scrobbler_mac: " << expr;                                                                            \
    } while (0)
#else
#define LFM_DEBUG(expr)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#endif

#define LFM_INFO(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        console::formatter f;                                                                                          \
        f << "foo_scrobbler_mac: " << expr;                                                                            \
    } while (0)
