//
//  lastfm_rules.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

namespace LastfmScrobbleConfig
{
static constexpr double MIN_TRACK_DURATION_SECONDS = 30.0;
static constexpr double SCROBBLE_THRESHOLD_FACTOR = 0.5;
static constexpr double MAX_THRESHOLD_SECONDS = 240.0;
static constexpr double LONG_TRACK_SECONDS = 480.0;
static constexpr double DELTA = 20.0;
} // namespace LastfmScrobbleConfig

struct LastfmRules
{
    double trackDuration = 0.0;
    double playbackTime = 0.0;
    bool paused = false;
    bool skippedEarly = false;

    bool isEligibleToScrobble() const
    {
        using namespace LastfmScrobbleConfig;

        if (trackDuration < MIN_TRACK_DURATION_SECONDS)
            return false;

        const double fiftyPercent = trackDuration * SCROBBLE_THRESHOLD_FACTOR;
        const double requiredTime = (trackDuration > LONG_TRACK_SECONDS) ? MAX_THRESHOLD_SECONDS : fiftyPercent;

        return playbackTime >= requiredTime;
    }

    bool shouldScrobble() const
    {
        if (paused || skippedEarly)
            return false;
        return isEligibleToScrobble();
    }

    void reset(double newDuration)
    {
        trackDuration = newDuration;
        playbackTime = 0.0;
        paused = false;
        skippedEarly = false;
    }

    void markSkipped()
    {
        skippedEarly = true;
    }
};
