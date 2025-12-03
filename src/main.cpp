//
//  main.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include <foobar2000/SDK/foobar2000.h>
#include "version.h"
#include "debug.h"

// Component GUID
static const GUID g_foo_scrobbler_mac_guid = {
    0xd490c10d, 0x37fe, 0x4075, {0x80, 0xb6, 0xa2, 0x4e, 0xc6, 0x16, 0x14, 0x79}};

// Component version info
DECLARE_COMPONENT_VERSION("Foo Scrobbler", FOOSCROBBLER_VERSION,
                          "A Last.fm scrobbler for foobar2000 (macOS).\n"
                          "(c) 2025 Konstantinos Kyriakopoulos.\n"
                          "GPLv3-licensed source.");

// Ensures the binary filename is correct
VALIDATE_COMPONENT_FILENAME("foo_scrobbler_mac.component");

// Init/quit handler
class foo_scrobbler_mac_component : public initquit
{
  public:
    void on_init() override
    {
        console::formatter f;
        f << FOOSCROBBLER_NAME << " " << FOOSCROBBLER_VERSION;
    }

    void on_quit() override
    {
        // No cleanup needed
    }
};

static initquit_factory_t<foo_scrobbler_mac_component> g_initquit_factory;
