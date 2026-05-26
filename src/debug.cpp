//
//  debug.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "debug.h"
#include "lastfm_settings.h"

namespace
{
int lastfmLogLevelFromConsoleChoice(int choice)
{
    return (choice == lastfm::settings::ConsoleNone)    ? static_cast<int>(LfmLogLevel::OFF)
           : (choice == lastfm::settings::ConsoleBasic) ? static_cast<int>(LfmLogLevel::INFO)
                                                        : static_cast<int>(LfmLogLevel::DEBUG_LOG);
}
} // namespace

std::atomic<int> lfmLogLevel{static_cast<int>(LfmLogLevel::INFO)};

void lastfmSetLogLevelFromConsoleChoice(int choice)
{
    lfmLogLevel.store(lastfmLogLevelFromConsoleChoice(choice), std::memory_order_relaxed);
}

void lastfmSyncLogLevelFromPrefs()
{
    lastfmSetLogLevelFromConsoleChoice(lastfm::settings::consoleLevel());
}
