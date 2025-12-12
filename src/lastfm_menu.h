//
//  lastfm_menu.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

class LastfmMenu : public mainmenu_commands
{
  public:
    enum
    {
        CMD_AUTHENTICATE = 0,
        CMD_CLEAR_AUTH,
        CMD_SUSPEND,
        CMD_COUNT
    };

    t_uint32 get_command_count() override;
    GUID get_command(t_uint32 index) override;
    void get_name(t_uint32 index, pfc::string_base& out) override;
    bool get_description(t_uint32 index, pfc::string_base& out) override;
    GUID get_parent() override;
    t_uint32 get_sort_priority() override;
    bool get_display(t_uint32 index, pfc::string_base& text, uint32_t& flags) override;
    void execute(t_uint32 index, ctx_t callback) override;
};
