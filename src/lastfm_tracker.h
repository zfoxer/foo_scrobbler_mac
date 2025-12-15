//
//  lastfm_tracker.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

#include <ctime>

#include "lastfm_rules.h"
#include "lastfm_track_info.h"

class LastfmTracker : public play_callback_static
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
    void resetState();
    void submitScrobbleIfNeeded();
    void updateFromTrack(const metadb_handle_ptr& track);

    std::time_t startWallclock = 0;
    bool isPlaying = false;
    bool scrobbleSent = false;
    double playbackTime = 0.0;

    LastfmTrackInfo current;

    double effectiveListenedSeconds = 0.0;
    double lastReportedTime = 0.0;
    bool haveLastReportedTime = false;

    // Reached scrobble threshold, but artist/title were missing at the moment.
    // We keep tracking tag changes and will submit once metadata becomes valid.
    bool pendingDueToMissingMetadata = false;

    // Track became eligible while suspended; defer submission until stop/new-track boundary.
    bool thresholdReachedButDeferred = false;

    metadb_handle_ptr currentHandle;
    LastfmRules rules;
};
