//
//  lastfm_tracker.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_exclusion_filters.h"
#include "lastfm_tracker.h"
#include "lastfm_core.h"
#include "lastfm_settings.h"
#include "lastfm_state.h"
#include "lastfm_util.h"
#include "debug.h"

#include <atomic>
#include <ctime>
#include <string>

namespace
{
static std::string evalTitleFormat(const metadb_handle_ptr& track, const service_ptr_t<titleformat_object>& script)
{
    if (!track.is_valid() || !script.is_valid())
        return {};

    pfc::string8 out;
    track->format_title(nullptr, out, script, nullptr);
    return lastfm::util::cleanTagValue(out.c_str());
}

static bool trackIsNetworkStream(const metadb_handle_ptr& track)
{
    if (!track.is_valid())
        return false;

    return lastfm::util::isNetworkStreamPath(track->get_path());
}

static void applyVariousArtistsRule(std::string& albumArtist)
{
    if (!lastfm::settings::treatVariousArtistsAsEmpty())
        return;

    if (albumArtist.empty())
        return;

    if (lastfm::util::isVariousArtistsValue(albumArtist))
        albumArtist.clear();
}

static bool isTrackInMediaLibrary(const metadb_handle_ptr& track)
{
    if (!track.is_valid())
        return false;

    static_api_ptr_t<library_manager> lm;
    return lm->is_item_in_library(track);
}

static int dynamicSourcesMode()
{
    const bool libraryOnly = lastfm::settings::onlyScrobbleFromMediaLibrary();
    const int configuredMode = lastfm::settings::configuredDynamicSourcesMode();

    if (libraryOnly && configuredMode != lastfm::settings::DynamicSourcesNone)
    {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LFM_DEBUG("Dynamic sources: overridden to 'No dynamic sources' because Only-from-library is enabled.");
    }

    return libraryOnly ? lastfm::settings::DynamicSourcesNone : configuredMode;
}

} // namespace

void LastfmTracker::recompileTfIfNeeded()
{
    static_api_ptr_t<titleformat_compiler> compiler;

    auto compileIfChanged =
        [&](const std::string& expr, std::string& cachedExpr, service_ptr_t<titleformat_object>& script)
    {
        if (expr == cachedExpr)
            return;

        cachedExpr = expr;
        script.release();
        if (!expr.empty())
            compiler->compile_safe(script, expr.c_str());
    };

    compileIfChanged(lastfm::settings::artistTitleFormat(), cachedArtistTfExpr_, artistTf_);
    compileIfChanged(lastfm::settings::albumArtistTitleFormat(), cachedAlbumArtistTfExpr_, albumArtistTf_);
    compileIfChanged(lastfm::settings::titleTitleFormat(), cachedTitleTfExpr_, titleTf_);
    compileIfChanged(lastfm::settings::albumTitleFormat(), cachedAlbumTfExpr_, albumTf_);
    compileIfChanged(lastfm::settings::mbidTitleFormat(), cachedMbidTfExpr_, mbidTf_);

    if (!fallbackArtistTf_.is_valid())
        compiler->compile_safe(fallbackArtistTf_, "[%Artist%]");
}

void LastfmTracker::fillTrackInfoFromTf(const metadb_handle_ptr& track, LastfmTrackInfo& out)
{
    recompileTfIfNeeded();

    out.artist = evalTitleFormat(track, artistTf_);
    out.title = evalTitleFormat(track, titleTf_);
    out.album = evalTitleFormat(track, albumTf_);
    out.albumArtist = evalTitleFormat(track, albumArtistTf_);
    out.mbid = evalTitleFormat(track, mbidTf_);

    applyVariousArtistsRule(out.albumArtist);

    if (lastfm::settings::treatVariousArtistsAsEmpty() && lastfm::util::isVariousArtistsValue(out.artist) &&
        out.albumArtist.empty())
    {
        std::string fallbackArtist = evalTitleFormat(track, fallbackArtistTf_);
        if (!fallbackArtist.empty())
            out.artist = fallbackArtist;
    }
}

unsigned LastfmTracker::get_flags()
{
    return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_time | flag_on_playback_seek |
           flag_on_playback_pause | flag_on_playback_edited | flag_on_playback_dynamic_info |
           flag_on_playback_dynamic_info_track;
}

void LastfmTracker::resetState()
{
    isPlaying = false;
    playbackTime = 0.0;
    channel = PlaybackChannel::None;
    currentFooScrobblerTagAllows = true;
    fooScrobblerTagBlockLogged = false;
    wasSuspended = lastfmIsSuspended();

    resetLocalChannelState();
    rules.reset(0.0);
    current = LastfmTrackInfo{};
    currentHandle.release();
    startWallclock = 0;

    resetDynamicChannelState();
}

void LastfmTracker::resetLocalChannelState()
{
    local = LocalChannelState{};
}

void LastfmTracker::resetDynamicChannelState()
{
    dynamic = DynamicChannelState{};
}

void LastfmTracker::updateListeningClock(ListenClock& clock, double time, bool blocked)
{
    if (blocked)
    {
        // Avoid a big delta jump when resuming.
        clock.haveLastReportedTime = false;
        return;
    }

    if (!clock.haveLastReportedTime)
    {
        clock.lastReportedTime = time;
        clock.haveLastReportedTime = true;
        return;
    }

    const double delta = time - clock.lastReportedTime;
    if (delta > 0.0 && delta <= LastfmScrobbleConfig::DELTA)
        clock.effectiveSeconds += delta;

    clock.lastReportedTime = time;
}

bool LastfmTracker::refreshFooScrobblerTagAllows()
{
    if (channel == PlaybackChannel::DynamicStream || !currentHandle.is_valid())
    {
        currentFooScrobblerTagAllows = true;
        return true;
    }

    file_info_impl info;
    if (!currentHandle->get_info(info))
    {
        currentFooScrobblerTagAllows = true;
        return true;
    }

    currentFooScrobblerTagAllows = lastfm::util::fooScrobblerTagAllowsSubmission(info);
    return currentFooScrobblerTagAllows;
}

void LastfmTracker::resendNowPlayingAfterResume()
{
    refreshFooScrobblerTagAllows();

    if (!currentFooScrobblerTagAllows || current.artist.empty() || current.title.empty())
        return;

    LastfmCore::instance().scrobbler().sendNowPlayingOnly(current);
}

bool LastfmTracker::trackIsExcluded(const LastfmTrackInfo& track, const file_info* externalInfo)
{
    return lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(track.artist, track.title, track.album) ||
           lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(currentHandle, track, externalInfo);
}

bool LastfmTracker::currentTrackIsExcluded(const file_info* externalInfo)
{
    return trackIsExcluded(current, externalInfo);
}

void LastfmTracker::refreshCurrentFileMetadata(bool allowDispatch)
{
    if (!isPlaying || channel != PlaybackChannel::LocalFile || !currentHandle.is_valid())
        return;

    if (local.state != LocalScrobbleState::Submitted && local.state != LocalScrobbleState::WaitingForMetadata)
        return;

    file_info_impl info;
    if (!currentHandle->get_info(info))
        return;

    currentFooScrobblerTagAllows = lastfm::util::fooScrobblerTagAllowsSubmission(info);

    LastfmTrackInfo refreshed = current;
    fillTrackInfoFromTf(currentHandle, refreshed);

    const bool changed = refreshed.artist != current.artist || refreshed.title != current.title ||
                         refreshed.album != current.album || refreshed.albumArtist != current.albumArtist ||
                         refreshed.mbid != current.mbid;
    if (!changed)
        return;

    current.artist = refreshed.artist;
    current.title = refreshed.title;
    current.album = refreshed.album;
    current.albumArtist = refreshed.albumArtist;
    current.mbid = refreshed.mbid;

    const bool hasRequiredMetadata = !current.artist.empty() && !current.title.empty();
    if (local.state == LocalScrobbleState::WaitingForMetadata && hasRequiredMetadata)
        local.state = LocalScrobbleState::Tracking;

    if (!hasRequiredMetadata)
        return;

    if (!allowDispatch || lastfmIsSuspended() || !currentFooScrobblerTagAllows)
        return;

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(current.artist, current.title, current.album) ||
        lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(currentHandle, current, &info))
        return;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    if (local.state == LocalScrobbleState::Submitted)
        scrobbler.refreshPendingMetadata(current);

    scrobbler.sendNowPlayingOnly(current);
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

    currentFooScrobblerTagAllows =
        channel == PlaybackChannel::DynamicStream || lastfm::util::fooScrobblerTagAllowsSubmission(info);

    fillTrackInfoFromTf(track, current);

    // Do NOT split TITLE for network streams at track-start.
    // Many streams put station info in TITLE like "Station - something" and we'd spam NP.
    if (channel != PlaybackChannel::DynamicStream && current.artist.empty() && !current.title.empty())
    {
        std::string a, t;
        if (lastfm::util::parseArtistTitleFromCombined(current.title, a, t))
        {
            current.artist = a;
            current.title = t;
        }
    }

    current.durationSeconds = info.get_length();
    rules.reset(current.durationSeconds);
}

void LastfmTracker::on_playback_new_track(metadb_handle_ptr track)
{
    const bool newIsStream = trackIsNetworkStream(track);
    LFM_DEBUG("Track path: " << (track->get_path() ? track->get_path() : "<null>")
                             << " stream=" << (newIsStream ? "yes" : "no"));

    // Natural boundary: submit previous track (if eligible) before switching state.
    rules.paused = false;
    submitDynamicPendingIfAny();
    submitLocalScrobbleIfNeeded(false);
    LastfmCore::instance().scrobbler().retryAsync();

    resetState();
    channel = newIsStream ? PlaybackChannel::DynamicStream : PlaybackChannel::LocalFile;
    isPlaying = true;
    startWallclock = std::time(nullptr);

    updateFromTrack(track);
    if (channel == PlaybackChannel::LocalFile)
        local.state = LocalScrobbleState::Tracking;

    if (current.artist.empty() || current.title.empty())
    {
        if (channel == PlaybackChannel::DynamicStream)
        {
            LFM_DEBUG("Stream: missing artist/title at start, waiting for dynamic metadata.");
            dynamic.segmentState = DynamicSegmentState::WaitingForMetadata;
            return;
        }

        LFM_INFO("Missing track info, not submitting.");
        return;
    }

    if (lastfm::settings::onlyScrobbleFromMediaLibrary() && !isTrackInMediaLibrary(track))
    {
        LFM_DEBUG("Track deferred: not in Media Library.");
        return;
    }

    if (currentTrackIsExcluded())
    {
        LFM_DEBUG("Track deferred: excluded by filters.");
        if (channel == PlaybackChannel::DynamicStream)
            dynamic.segmentState = DynamicSegmentState::WaitingForFilterRecovery;
        else
            local.state = LocalScrobbleState::WaitingForFilterRecovery;
        return;
    }

    if (lastfmIsSuspended() || !currentFooScrobblerTagAllows)
        return;

    LFM_DEBUG("Now playing: " << current.artist.c_str() << " - " << current.title.c_str());

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.onNowPlaying(current);
}

void LastfmTracker::on_playback_time(double time)
{
    playbackTime = time;

    // currentFooScrobblerTagAllows is kept fresh at track start, on tag edits and right before submission.
    const bool suspended = lastfmIsSuspended();

    // Resume from suspension: re-send Now Playing for the current track (NP-only path).
    if (wasSuspended && !suspended)
        resendNowPlayingAfterResume();
    wasSuspended = suspended;

    const bool blocked = suspended || !currentFooScrobblerTagAllows;

    if (!suspended && !currentFooScrobblerTagAllows)
    {
        if (!fooScrobblerTagBlockLogged)
        {
            LFM_DEBUG("Track ignored: FOO_SCROBBLER tag disabled.");
            fooScrobblerTagBlockLogged = true;
        }
    }
    else
    {
        fooScrobblerTagBlockLogged = false;
    }

    // Policy: while suspended or tag-disabled, freeze scrobble progress (do not count time).
    if (isPlaying && channel == PlaybackChannel::LocalFile && current.durationSeconds > 0.0)
        updateListeningClock(local.clock, time, blocked);
    else if (isPlaying && channel == PlaybackChannel::DynamicStream)
        updateListeningClock(dynamic.clock, time, blocked);

    if (channel == PlaybackChannel::DynamicStream)
    {
        // Stream-only: cache a dynamic scrobble payload once we have >=30s effective listening.
        maybeCacheDynamicScrobble(true);
        return;
    }

    // If we deferred an eligible scrobble while blocked, do not fire mid-track after unblock.
    // It will be handled on stop / new-track boundaries.
    if (local.state == LocalScrobbleState::DeferredUntilBoundary)
        return;

    submitLocalScrobbleIfNeeded(true);
}

void LastfmTracker::on_playback_seek(double)
{
    // Re-baseline both clocks so seek distance is never counted as listened time.
    local.clock.haveLastReportedTime = false;
    dynamic.clock.haveLastReportedTime = false;
}

void LastfmTracker::on_playback_pause(bool paused)
{
    rules.paused = paused;
}

void LastfmTracker::on_playback_stop(play_control::t_stop_reason)
{
    // A pause at the boundary must not veto an already-eligible scrobble.
    rules.paused = false;
    submitDynamicPendingIfAny();
    submitLocalScrobbleIfNeeded(false);
    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.retryAsync();
    resetState();
}

void LastfmTracker::submitLocalScrobbleIfNeeded(bool allowFilterRecovery)
{
    if (channel != PlaybackChannel::LocalFile)
        return;

    if (!isPlaying || local.state == LocalScrobbleState::Submitted || current.durationSeconds <= 0.0)
        return;

    if (local.state == LocalScrobbleState::BlockedByExclusionFilters)
        return;

    if (!rules.shouldScrobble())
        return;

    // Policy: Only submit from Media Library
    if (lastfm::settings::onlyScrobbleFromMediaLibrary() && currentHandle.is_valid())
    {
        if (!isTrackInMediaLibrary(currentHandle))
            return;
    }

    const double duration = current.durationSeconds;
    if (duration < LastfmScrobbleConfig::MIN_TRACK_DURATION_SECONDS)
        return;

    const double threshold = rules.requiredPlaybackSeconds();

    if (local.clock.effectiveSeconds < threshold)
        return;

    refreshFooScrobblerTagAllows();

    // Last-moment refresh if mandatory tags look missing.
    if (currentHandle.is_valid())
    {
        file_info_impl info;
        if (currentHandle->get_info(info))
            fillTrackInfoFromTf(currentHandle, current);
    }

    // If still missing after refresh, block and wait for tag update.
    if (current.artist.empty() || current.title.empty())
    {
        if (local.state != LocalScrobbleState::WaitingForMetadata)
            LFM_INFO("Scrobble blocked: Missing track info (artist/title). Will retry when tags update.");
        local.state = LocalScrobbleState::WaitingForMetadata;
        return;
    }

    if (local.state == LocalScrobbleState::WaitingForMetadata)
        local.state = LocalScrobbleState::Tracking;

    if (currentTrackIsExcluded())
    {
        if (local.state != LocalScrobbleState::BlockedByExclusionFilters)
            LFM_DEBUG("Scrobble skipped: excluded by filters.");
        local.state = LocalScrobbleState::BlockedByExclusionFilters;
        return;
    }

    if (local.state == LocalScrobbleState::WaitingForFilterRecovery && !allowFilterRecovery)
        return;

    if (local.state == LocalScrobbleState::WaitingForFilterRecovery)
        local.state = LocalScrobbleState::Tracking;

    // Eligible, but suspended/tag-disabled -> remember and defer.
    if (lastfmIsSuspended() || !currentFooScrobblerTagAllows)
    {
        local.state = LocalScrobbleState::DeferredUntilBoundary;
        return;
    }

    if (!lastfmIsAuthenticated())
        return;

    local.state = LocalScrobbleState::Submitted;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.queueScrobble(current, playbackTime, startWallclock, /*refreshOnSubmit=*/true);
}

void LastfmTracker::handleDynamicStreamUpdate(const file_info& info)
{
    if (!isPlaying || !currentHandle.is_valid())
        return;

    // Stream-only path. Library/local behavior never enters here.
    if (channel != PlaybackChannel::DynamicStream)
        return;

    const int mode = dynamicSourcesMode();
    if (mode == 0)
        return;

    std::string newArtist, newTitle, newAlbum;
    if (!lastfm::util::extractStreamArtistTitle(info, newArtist, newTitle, newAlbum))
        return;

    // Generic filter: station branding etc.
    if (lastfm::util::looksLikeStationTitle(newTitle))
    {
        LFM_DEBUG("Stream dynamic ignored (looksLikeStationTitle): " << newTitle.c_str());
        return;
    }

    // De-dupe dynamic metadata updates (foobar may call both dynamic callbacks for the same change).
    // Keyed by stream URL + artist + title so the same track on another station still passes.
    const char* p = currentHandle->get_path();
    const std::string path = p ? p : "";

    if (path != dynamic.dedupLastPath)
    {
        dynamic.dedupLastPath = path;
        dynamic.dedupLastArtist.clear();
        dynamic.dedupLastTitle.clear();
    }

    if (newArtist == dynamic.dedupLastArtist && newTitle == dynamic.dedupLastTitle)
        return;

    dynamic.dedupLastArtist = newArtist;
    dynamic.dedupLastTitle = newTitle;

    // Apply the user's title-format scripts to stream metadata so streams honor the same formatting
    // (e.g. trimming "(...)" / "[...]") as local files. extractStreamArtistTitle() only reads raw tags.
    applyTitleFormatToStreamMetadata(info, newArtist, newTitle, newAlbum);

    if (newArtist == current.artist && newTitle == current.title && newAlbum == current.album)
        return;

    // Only do scrobble-related work in mode 2.
    if (mode == 2)
    {
        // We are about to switch to a new stream "track" (dynamic metadata change).
        // If the previous segment has already reached >=30s, it should be cached.
        maybeCacheDynamicScrobble(false);
        // Submit the previous segment (if cached) BEFORE switching to the new one.
        submitDynamicPendingIfAny();
    }

    const bool wasWaitingForMetadata = dynamic.segmentState == DynamicSegmentState::WaitingForMetadata;

    current.artist = newArtist;
    current.title = newTitle;
    current.album = newAlbum;

    // Start a new dynamic segment from this point.
    startDynamicSegment();
    dynamic.currentSegmentInfo.copy(info);
    dynamic.haveCurrentSegmentInfo = true;

    if (currentTrackIsExcluded(&info))
    {
        LFM_DEBUG("Stream dynamic deferred: excluded by filters.");
        dynamic.segmentState = DynamicSegmentState::WaitingForFilterRecovery;
        return;
    }

    if (lastfmIsSuspended())
        return;

    auto& scrobbler = LastfmCore::instance().scrobbler();

    // If we were waiting for dynamic metadata, this is the "start" of the stream track.
    if (wasWaitingForMetadata)
    {
        if (lastfm::settings::disableNowPlaying())
        {
            LFM_DEBUG("Dynamic NP suppressed (stream start): " << current.artist.c_str() << " - "
                                                               << current.title.c_str());
        }
        else
        {
            LFM_DEBUG("Submitting dynamic NP (stream start): " << current.artist.c_str() << " - "
                                                               << current.title.c_str());
            scrobbler.onNowPlaying(current);
        }
        return;
    }

    // Otherwise it's an update / track change.
    if (lastfm::settings::disableNowPlaying())
    {
        LFM_DEBUG("NP suppressed (dynamic): " << current.artist.c_str() << " - " << current.title.c_str());
    }
    else
    {
        LFM_DEBUG("Submitting NP (dynamic): " << current.artist.c_str() << " - " << current.title.c_str());
        scrobbler.sendNowPlayingOnly(current);
    }
}

void LastfmTracker::applyTitleFormatToStreamMetadata(const file_info& info, std::string& artist, std::string& title,
                                                     std::string& album)
{
    if (!currentHandle.is_valid())
        return;

    recompileTfIfNeeded();

    // Write the parsed values into a temporary info so scripts referencing %artist%/%title%/%album%
    // see what we actually scrobble, not the raw combined stream tag (e.g. "Artist - Title").
    file_info_impl tfInfo;
    tfInfo.copy(info);
    tfInfo.meta_set("ARTIST", artist.c_str());
    tfInfo.meta_set("TITLE", title.c_str());
    tfInfo.meta_set("ALBUM", album.c_str());

    auto evalExternal = [&](const service_ptr_t<titleformat_object>& script) -> std::string
    {
        if (!script.is_valid())
            return {};

        pfc::string8 out;
        currentHandle->format_title_from_external_info(tfInfo, nullptr, out, script, nullptr);
        return lastfm::util::cleanTagValue(out.c_str());
    };

    const std::string formattedArtist = evalExternal(artistTf_);
    if (!formattedArtist.empty())
        artist = formattedArtist;

    const std::string formattedTitle = evalExternal(titleTf_);
    if (!formattedTitle.empty())
        title = formattedTitle;

    const std::string formattedAlbum = evalExternal(albumTf_);
    if (!formattedAlbum.empty())
        album = formattedAlbum;
}

void LastfmTracker::startDynamicSegment()
{
    dynamic.segmentState = DynamicSegmentState::Tracking;
    dynamic.segmentStartWallclock = std::time(nullptr);
    dynamic.clock = ListenClock{};
}

void LastfmTracker::maybeCacheDynamicScrobble(bool allowFilterRecovery)
{
    // Only cache when dynamic scrobbling is enabled (mode 2).
    if (dynamicSourcesMode() != lastfm::settings::DynamicSourcesNowPlayingAndScrobbling)
        return;

    if (!currentHandle.is_valid() || channel != PlaybackChannel::DynamicStream)
        return;

    if (dynamic.segmentState != DynamicSegmentState::Tracking &&
        dynamic.segmentState != DynamicSegmentState::WaitingForFilterRecovery)
        return;

    if (dynamic.pendingState == DynamicPendingState::Cached || current.artist.empty() || current.title.empty())
        return;

    // Cache exactly once when effective listening reaches 30s.
    if (dynamic.clock.effectiveSeconds < 30.0)
        return;

    const file_info* currentSegmentInfo = dynamic.haveCurrentSegmentInfo ? &dynamic.currentSegmentInfo : nullptr;
    if (currentTrackIsExcluded(currentSegmentInfo))
    {
        if (dynamic.segmentState != DynamicSegmentState::WaitingForFilterRecovery)
            LFM_DEBUG("Stream scrobble skipped: excluded by filters.");
        dynamic.segmentState = DynamicSegmentState::WaitingForFilterRecovery;
        return;
    }

    if (dynamic.segmentState == DynamicSegmentState::WaitingForFilterRecovery && !allowFilterRecovery)
        return;

    dynamic.segmentState = DynamicSegmentState::Tracking;

    dynamic.pendingState = DynamicPendingState::Cached;
    dynamic.pendingTrack = current;
    dynamic.havePendingSegmentInfo = dynamic.haveCurrentSegmentInfo;
    if (dynamic.havePendingSegmentInfo)
        dynamic.pendingSegmentInfo.copy(dynamic.currentSegmentInfo);
    dynamic.pendingPlaybackTime = playbackTime;
    dynamic.pendingStartWallclock = dynamic.segmentStartWallclock;

    LFM_DEBUG("Stream scrobble cached: " << current.artist.c_str() << " - " << current.title.c_str());
}

void LastfmTracker::submitDynamicPendingIfAny()
{
    if (dynamic.pendingState != DynamicPendingState::Cached)
        return;

    if (!currentHandle.is_valid() || channel != PlaybackChannel::DynamicStream)
        return;

    if (dynamicSourcesMode() != lastfm::settings::DynamicSourcesNowPlayingAndScrobbling)
        return;

    // Keep global policy consistent. If user selected "only from Media Library", streams never scrobble.
    if (lastfm::settings::onlyScrobbleFromMediaLibrary())
        return;

    // Do not submit while suspended; keep it cached for the next boundary after resume.
    if (lastfmIsSuspended())
        return;

    if (!lastfmIsAuthenticated())
        return;

    const file_info* pendingSegmentInfo = dynamic.havePendingSegmentInfo ? &dynamic.pendingSegmentInfo : nullptr;
    if (trackIsExcluded(dynamic.pendingTrack, pendingSegmentInfo))
    {
        dynamic.pendingState = DynamicPendingState::Empty;
        dynamic.pendingTrack = LastfmTrackInfo{};
        dynamic.havePendingSegmentInfo = false;
        return;
    }

    dynamic.pendingState = DynamicPendingState::Empty;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.queueScrobble(dynamic.pendingTrack, dynamic.pendingPlaybackTime, dynamic.pendingStartWallclock,
                            /*refreshOnSubmit=*/true);
    dynamic.pendingTrack = LastfmTrackInfo{};
    dynamic.havePendingSegmentInfo = false;
    scrobbler.retryAsync();
}

void LastfmTracker::on_playback_dynamic_info(const file_info& info)
{
    handleDynamicStreamUpdate(info);
}

void LastfmTracker::on_playback_dynamic_info_track(const file_info& info)
{
    handleDynamicStreamUpdate(info);
}

// Remaining callbacks
void LastfmTracker::on_playback_starting(play_control::t_track_command, bool)
{
}
void LastfmTracker::on_playback_edited(metadb_handle_ptr)
{
    refreshFooScrobblerTagAllows();
    refreshCurrentFileMetadata(true);
}
void LastfmTracker::on_volume_change(float)
{
}

void LastfmTracker::queuePendingAtShutdown()
{
    rules.paused = false;
    maybeCacheDynamicScrobble(false);
    submitDynamicPendingIfAny();
    submitLocalScrobbleIfNeeded(false);
}

static play_callback_static_factory_t<LastfmTracker> lastfmTrackerFactory;

class LastfmTrackerShutdownHook : public initquit
{
  public:
    void on_quit() override
    {
        lastfmTrackerFactory.get_static_instance().queuePendingAtShutdown();
    }
};

static initquit_factory_t<LastfmTrackerShutdownHook> lastfmTrackerShutdownHookFactory;
