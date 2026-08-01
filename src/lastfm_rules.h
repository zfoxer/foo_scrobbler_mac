//
//  lastfm_rules.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

namespace LastfmScrobbleConfig
{
static constexpr double MIN_TRACK_DURATION_SECONDS = 30.0;
static constexpr double SCROBBLE_THRESHOLD_FACTOR = 0.5;
static constexpr double MAX_THRESHOLD_SECONDS = 240.0;
static constexpr double DELTA = 20.0;
} // namespace LastfmScrobbleConfig

struct LastfmRules
{
    double trackDuration = 0.0;
    bool paused = false;

    double requiredPlaybackSeconds() const
    {
        const double fiftyPercent = trackDuration * LastfmScrobbleConfig::SCROBBLE_THRESHOLD_FACTOR;
        return (fiftyPercent < LastfmScrobbleConfig::MAX_THRESHOLD_SECONDS)
                   ? fiftyPercent
                   : LastfmScrobbleConfig::MAX_THRESHOLD_SECONDS;
    }

    // Eligibility is accumulated-listening-time only (the tracker's ListenClock);
    // playhead position is deliberately not part of the rule.
    bool shouldScrobble() const
    {
        return !paused;
    }

    void reset(double newDuration)
    {
        trackDuration = newDuration;
        paused = false;
    }
};
