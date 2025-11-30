//
//  lastfm_rules.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

namespace lastfm_scrobble_config
{
static constexpr double MIN_TRACK_DURATION_SECONDS = 30.0;
static constexpr double SCROBBLE_THRESHOLD_FACTOR = 0.5;
static constexpr double MAX_THRESHOLD_SECONDS = 240.0;
static constexpr double LONG_TRACK_SECONDS = 480.0;
} // namespace lastfm_scrobble_config

struct lastfm_rules
{
    double track_duration = 0.0;
    double playback_time = 0.0;
    bool paused = false;
    bool skipped_early = false;

    bool is_eligible_to_scrobble() const
    {
        using namespace lastfm_scrobble_config;

        if (track_duration < MIN_TRACK_DURATION_SECONDS)
            return false;

        const double fifty_percent = track_duration * SCROBBLE_THRESHOLD_FACTOR;
        const double required_time = (track_duration > LONG_TRACK_SECONDS) ? MAX_THRESHOLD_SECONDS : fifty_percent;

        return playback_time >= required_time;
    }

    bool should_scrobble() const
    {
        if (paused || skipped_early)
            return false;
        return is_eligible_to_scrobble();
    }

    void reset(double new_duration)
    {
        track_duration = new_duration;
        playback_time = 0.0;
        paused = false;
        skipped_early = false;
    }

    void mark_skipped()
    {
        skipped_early = true;
    }
};
