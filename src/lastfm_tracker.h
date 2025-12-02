//
//  lastfm_tracker.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

#include <string>

#include "lastfm_rules.h"

struct lastfm_track_info
{
    std::string artist;
    std::string title;
    std::string album;
    std::string mbid;
    double duration_seconds = 0.0;
};

class lastfm_tracker : public play_callback_static
{
  public:
    unsigned get_flags() override;
    void on_playback_starting(play_control::t_track_command command, bool paused) override;
    void on_playback_new_track(metadb_handle_ptr track) override;
    void on_playback_stop(play_control::t_stop_reason reason) override;
    void on_playback_seek(double time) override;
    void on_playback_pause(bool paused) override;
    void on_playback_edited(metadb_handle_ptr track) override;
    void on_playback_dynamic_info(const file_info& info) override;
    void on_playback_dynamic_info_track(const file_info& info) override;
    void on_playback_time(double time) override;
    void on_volume_change(float volume) override;

  private:
    void reset_state();
    void submit_scrobble_if_needed();
    void update_from_track(const metadb_handle_ptr& track);

    bool m_is_playing = false;
    bool m_scrobble_sent = false;
    double m_playback_time = 0.0;
    lastfm_track_info m_current;
    // Effective listening time logic
    double m_effective_listened_seconds = 0.0;
    double m_last_reported_time = 0.0;
    bool m_have_last_reported_time = false;

    lastfm_rules m_rules;
};
