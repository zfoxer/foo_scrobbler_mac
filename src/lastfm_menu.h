//
//  lastfm_menu.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

class lastfm_menu : public mainmenu_commands
{
  public:
    enum
    {
        cmd_authenticate = 0,
        cmd_clear_auth,
        cmd_count
    };

    t_uint32 get_command_count() override;
    GUID get_command(t_uint32 index) override;
    void get_name(t_uint32 index, pfc::string_base& out) override;
    bool get_description(t_uint32 index, pfc::string_base& out) override;
    GUID get_parent() override;

    // NOTE: no parameter here – matches SDK exactly
    t_uint32 get_sort_priority() override;

    // Display/flags (disabled/checked) – matches SDK:
    // virtual bool get_display(t_uint32 p_index,pfc::string_base & p_text,uint32_t & p_flags)
    bool get_display(t_uint32 index, pfc::string_base& text, uint32_t& flags) override;

    // execute signature from your SDK:
    // virtual void execute(t_uint32 p_index,ctx_t p_callback) = 0;
    void execute(t_uint32 index, ctx_t callback) override;
};
