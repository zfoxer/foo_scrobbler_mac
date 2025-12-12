//
//  debug.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "debug.h"

std::atomic<int> lfmLogLevel{static_cast<int>(LfmLogLevel::INFO)};
