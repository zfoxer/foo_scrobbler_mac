//
//  debug.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "debug.h"

std::atomic<int> g_lfm_log_level{(int)LFMLogLevel::Info};
