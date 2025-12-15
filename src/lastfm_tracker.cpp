//
//  lastfm_tracker.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_tracker.h"
#include "lastfm_core.h"
#include "lastfm_state.h"
#include "debug.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>

namespace
{

static std::string cleanTagValue(const char* value)
{
    if (!value)
        return {};

    std::string s(value);

    std::size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;

    std::size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1]))
        --end;

    if (start == end)
        return {};

    s = s.substr(start, end - start);

    std::string norm;
    for (char c : s)
        if (!std::isspace((unsigned char)c))
            norm.push_back((char)std::tolower((unsigned char)c));

    if (norm == "unknown" || norm == "unknownartist" || norm == "unknowntrack")
        return {};

    return s;
}
} // namespace

unsigned LastfmTracker::get_flags()
{
    return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_time | flag_on_playback_seek |
           flag_on_playback_pause;
}

void LastfmTracker::resetState()
{
    isPlaying = false;
    scrobbleSent = false;
    playbackTime = 0.0;

    effectiveListenedSeconds = 0.0;
    lastReportedTime = 0.0;
    haveLastReportedTime = false;

    pendingDueToMissingMetadata = false;
    thresholdReachedButDeferred = false;

    rules.reset(0.0);
    current = LastfmTrackInfo{};
    currentHandle.release();
    startWallclock = 0;
}

void LastfmTracker::updateFromTrack(const metadb_handle_ptr& track)
{
    currentHandle = track;

    file_info_impl info;
    if (!track->get_info(info))
    {
        resetState();
        return;
    }

    current.artist = cleanTagValue(info.meta_get("artist", 0));
    current.title = cleanTagValue(info.meta_get("title", 0));
    current.album = cleanTagValue(info.meta_get("album", 0));

    const char* mbid = info.meta_get("musicbrainz_trackid", 0);
    current.mbid = mbid ? mbid : "";

    current.durationSeconds = info.get_length();
    rules.reset(current.durationSeconds);
}

void LastfmTracker::on_playback_new_track(metadb_handle_ptr track)
{
    // Natural boundary: if we deferred an eligible scrobble for the previous track, submit now.
    if (thresholdReachedButDeferred)
        submitScrobbleIfNeeded();

    resetState();
    isPlaying = true;
    startWallclock = std::time(nullptr);

    updateFromTrack(track);

    // While suspended: no Now Playing behavior at all.
    if (lastfm_is_suspended())
        return;

    LFM_DEBUG("Now playing: " << current.artist.c_str() << " - " << current.title.c_str());

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.onNowPlaying(current);
}

void LastfmTracker::on_playback_time(double time)
{
    playbackTime = time;

    const bool suspended = lastfm_is_suspended();

    // Policy B: while suspended, freeze scrobble progress (do not count time).
    if (!suspended)
    {
        if (isPlaying && current.durationSeconds > 0.0)
        {
            if (!haveLastReportedTime)
            {
                lastReportedTime = time;
                haveLastReportedTime = true;
            }
            else
            {
                const double delta = time - lastReportedTime;
                if (delta > 0.0 && delta <= LastfmScrobbleConfig::DELTA)
                    effectiveListenedSeconds += delta;

                lastReportedTime = time;
            }
        }

        rules.playbackTime = time;
    }
    else
    {
        // Avoid a big delta jump when resuming.
        haveLastReportedTime = false;
    }

    auto& scrobbler = LastfmCore::instance().scrobbler();
    if ((scrobbleSent || pendingDueToMissingMetadata) && currentHandle.is_valid())
    {
        file_info_impl info;
        if (currentHandle->get_info(info))
        {
            std::string newArtist = cleanTagValue(info.meta_get("artist", 0));
            std::string newTitle = cleanTagValue(info.meta_get("title", 0));
            std::string newAlbum = cleanTagValue(info.meta_get("album", 0));

            if (newArtist != current.artist || newTitle != current.title || newAlbum != current.album)
            {
                current.artist = newArtist;
                current.title = newTitle;
                current.album = newAlbum;

                if (!suspended)
                {
                    if (scrobbleSent)
                        scrobbler.refreshPendingMetadata(current);

                    scrobbler.sendNowPlayingOnly(current);
                }

                if (pendingDueToMissingMetadata && !current.artist.empty() && !current.title.empty())
                    pendingDueToMissingMetadata = false;
            }
        }
    }

    // If we deferred an eligible scrobble during suspension, do not fire mid-track after resume.
    // It will be handled on stop / new-track boundaries.
    if (thresholdReachedButDeferred)
        return;

    submitScrobbleIfNeeded();
}

void LastfmTracker::on_playback_seek(double time)
{
    if (!isPlaying || current.durationSeconds <= 0.0)
        return;

    const double half = current.durationSeconds * LastfmScrobbleConfig::SCROBBLE_THRESHOLD_FACTOR;

    if (time < half)
    {
        effectiveListenedSeconds = 0.0;
        haveLastReportedTime = false;
    }
}

void LastfmTracker::on_playback_pause(bool paused)
{
    rules.paused = paused;
}

void LastfmTracker::on_playback_stop(play_control::t_stop_reason)
{
    submitScrobbleIfNeeded();
    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.retryAsync();
    resetState();
}

void LastfmTracker::submitScrobbleIfNeeded()
{
    if (!isPlaying || scrobbleSent || current.durationSeconds <= 0.0)
        return;

    if (!rules.shouldScrobble())
        return;

    const double duration = current.durationSeconds;
    if (duration < LastfmScrobbleConfig::MIN_TRACK_DURATION_SECONDS)
        return;

    const double threshold = std::min(duration * LastfmScrobbleConfig::SCROBBLE_THRESHOLD_FACTOR,
                                      LastfmScrobbleConfig::MAX_THRESHOLD_SECONDS);

    if (effectiveListenedSeconds < threshold)
        return;

    // Last-moment refresh if mandatory tags look missing.
    if ((current.artist.empty() || current.title.empty()) && currentHandle.is_valid())
    {
        file_info_impl info;
        if (currentHandle->get_info(info))
        {
            current.artist = cleanTagValue(info.meta_get("artist", 0));
            current.title = cleanTagValue(info.meta_get("title", 0));
            current.album = cleanTagValue(info.meta_get("album", 0));
        }
    }

    // If still missing after refresh, block and wait for tag update.
    if (current.artist.empty() || current.title.empty())
    {
        if (!pendingDueToMissingMetadata)
            LFM_INFO("Scrobble blocked: Missing track info (artist/title). Will retry when tags update.");
        pendingDueToMissingMetadata = true;
        return;
    }

    pendingDueToMissingMetadata = false;

    // Eligible, but suspended -> remember and defer.
    if (lastfm_is_suspended())
    {
        thresholdReachedButDeferred = true;
        return;
    }

    if (!lastfm_is_authenticated())
        return;

    scrobbleSent = true;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.queueScrobble(current, playbackTime, startWallclock, /*refreshOnSubmit=*/true);
}

// Unused callbacks (required by interface)
void LastfmTracker::on_playback_starting(play_control::t_track_command, bool)
{
}
void LastfmTracker::on_playback_edited(metadb_handle_ptr)
{
}
void LastfmTracker::on_playback_dynamic_info(const file_info&)
{
}
void LastfmTracker::on_playback_dynamic_info_track(const file_info&)
{
}
void LastfmTracker::on_volume_change(float)
{
}

static play_callback_static_factory_t<LastfmTracker> lastfmTrackerFactory;
