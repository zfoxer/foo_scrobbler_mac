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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <string>
#include <cstring>

namespace
{
static bool isVariousArtistsValue(const std::string& value)
{
    std::string s = value;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s == "various artists";
}

static std::string evalTitleFormat(const metadb_handle_ptr& track, const service_ptr_t<titleformat_object>& script)
{
    if (!track.is_valid() || !script.is_valid())
        return {};

    pfc::string8 out;
    track->format_title(nullptr, out, script, nullptr);
    return lastfm::util::cleanTagValue(out.c_str());
}

static void applyVariousArtistsRule(std::string& albumArtist)
{
    if (!lastfm::settings::treatVariousArtistsAsEmpty())
        return;

    if (albumArtist.empty())
        return;

    std::string s = albumArtist;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (s == "various artists")
        albumArtist.clear();
}

static bool isTrackInMediaLibrary(const metadb_handle_ptr& track)
{
    if (!track.is_valid())
        return false;

    static_api_ptr_t<library_manager> lm;
    return lm->is_item_in_library(track);
}

static bool isNetworkStreamPath(const metadb_handle_ptr& track)
{
    if (!track.is_valid())
        return false;

    const char* p = track->get_path();
    if (!p)
        return false;

    // Be strict: foobar can use pseudo-schemes like foo:// for local container tracks (ISO, etc).
    // We only treat real network stream schemes as "stream".
    return (std::strncmp(p, "http://", 7) == 0) || (std::strncmp(p, "https://", 8) == 0) ||
           (std::strncmp(p, "mms://", 6) == 0) || (std::strncmp(p, "rtsp://", 7) == 0) ||
           (std::strncmp(p, "icy://", 6) == 0);
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

static bool looksLikeStationTitle(const std::string& title)
{
    if (title.empty())
        return true;

    // long sentences / slogans / blurbs / bs
    if (title.size() > 80)
        return true;

    int alpha = 0;
    int spaces = 0;

    bool hasBracket = false;
    bool hasUrl = false;

    std::string norm;
    norm.reserve(title.size());

    for (unsigned char c : title)
    {
        const char lc = (char)std::tolower(c);
        norm.push_back(lc);

        if (std::isalpha(c))
            ++alpha;
        else if (std::isspace(c))
            ++spaces;

        if (c == '[' || c == ']')
            hasBracket = true;
    }

    if (norm.find("http") != std::string::npos || norm.find("www.") != std::string::npos)
        hasUrl = true;

    if (alpha < 3)
        return true;

    if (hasBracket)
        return true;

    if (spaces > (int)title.size() / 3)
        return true;

    if (hasUrl)
        return true;

    return false;
}

static bool parseArtistTitleFromCombined(const std::string& combined, std::string& artist, std::string& title)
{
    const char* seps[] = {" - ", " – ", " — ", ": "};
    for (const char* sep : seps)
    {
        const std::size_t pos = combined.find(sep);
        if (pos == std::string::npos)
            continue;

        const std::string left = combined.substr(0, pos);
        const std::string right = combined.substr(pos + std::strlen(sep));

        artist = lastfm::util::cleanTagValue(left.c_str());
        title = lastfm::util::cleanTagValue(right.c_str());

        if (artist.empty() || title.empty())
            continue;

        if (looksLikeStationTitle(title))
            continue;

        return true;
    }
    return false;
}

static bool extractStreamArtistTitle(const file_info& info, std::string& outArtist, std::string& outTitle,
                                     std::string& outAlbum)
{
    outArtist.clear();
    outTitle.clear();
    outAlbum.clear();

    auto get1 = [&](const char* key) -> std::string { return lastfm::util::cleanTagValue(info.meta_get(key, 0)); };

    auto firstOf = [&](const char* const* keys, std::size_t n) -> std::string
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            std::string v = get1(keys[i]);
            if (!v.empty())
                return v;
        }
        return {};
    };

    // Try the common “combined” stream title fields first (usually "Artist - Title")
    // These names vary across decoders, so we probe a small generic set.
    static const char* kCombined[] = {
        "streamtitle", "StreamTitle", "STREAMTITLE", "icy-title",
        "Icy-Title",   "ICY-TITLE",   "title",       "TITLE" // last resort, but still parsed as combined if possible
    };

    std::string combined = firstOf(kCombined, sizeof(kCombined) / sizeof(kCombined[0]));
    if (!combined.empty())
    {
        std::string a, t;
        if (parseArtistTitleFromCombined(combined, a, t))
        {
            outArtist = a;
            outTitle = t;
            return true;
        }
    }

    // If no combined parse, try explicit artist/title tags
    static const char* kArtist[] = {"artist", "ARTIST"};
    static const char* kTitle[] = {"title", "TITLE"};
    static const char* kAlbum[] = {"album", "ALBUM"};

    std::string a = firstOf(kArtist, sizeof(kArtist) / sizeof(kArtist[0]));
    std::string t = firstOf(kTitle, sizeof(kTitle) / sizeof(kTitle[0]));
    std::string al = firstOf(kAlbum, sizeof(kAlbum) / sizeof(kAlbum[0]));

    // If title looks like station branding/slogan, reject it.
    if (!t.empty() && looksLikeStationTitle(t))
        t.clear();

    if (!a.empty() && !t.empty())
    {
        outArtist = a;
        outTitle = t;
        outAlbum = al;
        return true;
    }

    return false;
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

    applyVariousArtistsRule(out.albumArtist);

    if (lastfm::settings::treatVariousArtistsAsEmpty() && isVariousArtistsValue(out.artist) && out.albumArtist.empty())
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
    scrobbleSent = false;
    playbackTime = 0.0;
    isCurrentStream = false;

    effectiveListenedSeconds = 0.0;
    lastReportedTime = 0.0;
    haveLastReportedTime = false;
    currentFooScrobblerTagAllows = true;
    fooScrobblerTagBlockLogged = false;

    pendingDueToMissingMetadata = false;
    thresholdReachedButDeferred = false;

    rules.reset(0.0);
    current = LastfmTrackInfo{};
    currentHandle.release();
    startWallclock = 0;

    resetDynamicSegmentState();
}

bool LastfmTracker::refreshFooScrobblerTagAllows()
{
    if (isCurrentStream || !currentHandle.is_valid())
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

void LastfmTracker::refreshCurrentFileMetadata(bool allowDispatch)
{
    if (!isPlaying || isCurrentStream || !currentHandle.is_valid())
        return;

    if (!scrobbleSent && !pendingDueToMissingMetadata)
        return;

    file_info_impl info;
    if (!currentHandle->get_info(info))
        return;

    currentFooScrobblerTagAllows = lastfm::util::fooScrobblerTagAllowsSubmission(info);

    LastfmTrackInfo refreshed = current;
    fillTrackInfoFromTf(currentHandle, refreshed);

    const bool changed = refreshed.artist != current.artist || refreshed.title != current.title ||
                         refreshed.album != current.album || refreshed.albumArtist != current.albumArtist;
    if (!changed)
        return;

    current.artist = refreshed.artist;
    current.title = refreshed.title;
    current.album = refreshed.album;
    current.albumArtist = refreshed.albumArtist;

    const bool hasRequiredMetadata = !current.artist.empty() && !current.title.empty();
    if (pendingDueToMissingMetadata && hasRequiredMetadata)
        pendingDueToMissingMetadata = false;

    if (!hasRequiredMetadata)
        return;

    if (!allowDispatch || lastfmIsSuspended() || !currentFooScrobblerTagAllows)
        return;

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(current.artist, current.title, current.album) ||
        lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(currentHandle, current, &info))
        return;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    if (scrobbleSent)
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

    currentFooScrobblerTagAllows = isCurrentStream || lastfm::util::fooScrobblerTagAllowsSubmission(info);

    fillTrackInfoFromTf(track, current);

    // Do NOT split TITLE for network streams at track-start.
    // Many streams put station info in TITLE like "Station - something" and we'd spam NP.
    if (!isCurrentStream && current.artist.empty() && !current.title.empty())
    {
        std::string a, t;
        if (parseArtistTitleFromCombined(current.title, a, t))
        {
            current.artist = a;
            current.title = t;
        }
    }

    const char* mbid = info.meta_get("musicbrainz_trackid", 0);
    if (!mbid)
        mbid = info.meta_get("MUSICBRAINZ_TRACKID", 0);
    current.mbid = mbid ? mbid : "";

    current.durationSeconds = info.get_length();
    rules.reset(current.durationSeconds);
}

void LastfmTracker::on_playback_new_track(metadb_handle_ptr track)
{
    const bool newIsStream = isNetworkStreamPath(track);
    LFM_DEBUG("Track path: " << (track->get_path() ? track->get_path() : "<null>")
                             << " stream=" << (newIsStream ? "yes" : "no"));

    // Natural boundary: submit previous track (if eligible) before switching state.
    submitDynamicPendingIfAny();
    submitScrobbleIfNeeded();
    LastfmCore::instance().scrobbler().retryAsync();

    resetState();
    isCurrentStream = newIsStream;
    isPlaying = true;
    startWallclock = std::time(nullptr);

    updateFromTrack(track);

    if (current.artist.empty() || current.title.empty())
    {
        if (isCurrentStream)
        {
            LFM_DEBUG("Stream: missing artist/title at start, waiting for dynamic metadata.");
            pendingDueToMissingMetadata = true;
            return;
        }

        LFM_INFO("Missing track info, not submitting.");
        return;
    }

    if (lastfm::settings::onlyScrobbleFromMediaLibrary() && !isTrackInMediaLibrary(track))
    {
        LFM_DEBUG("Track skipped: not in Media Library.");
        resetState();
        return;
    }

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(current.artist, current.title, current.album) ||
        lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(track, current))
    {
        LFM_DEBUG("Track skipped: excluded by filters.");
        resetState();
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

    const bool suspended = lastfmIsSuspended();
    refreshFooScrobblerTagAllows();
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
    if (!blocked)
    {
        if (isPlaying && (current.durationSeconds > 0.0 || isCurrentStream))
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

    refreshCurrentFileMetadata(!blocked);

    // Stream-only: cache a dynamic scrobble payload once we have >=30s effective listening.
    maybeCacheDynamicScrobble();

    // If we deferred an eligible scrobble while blocked, do not fire mid-track after unblock.
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
    submitDynamicPendingIfAny();
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

    if (effectiveListenedSeconds < threshold)
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
        if (!pendingDueToMissingMetadata)
            LFM_INFO("Scrobble blocked: Missing track info (artist/title). Will retry when tags update.");
        pendingDueToMissingMetadata = true;
        return;
    }

    pendingDueToMissingMetadata = false;

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(current.artist, current.title, current.album) ||
        lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(currentHandle, current))
        return;

    // Eligible, but suspended/tag-disabled -> remember and defer.
    if (lastfmIsSuspended() || !currentFooScrobblerTagAllows)
    {
        thresholdReachedButDeferred = true;
        return;
    }

    if (!lastfmIsAuthenticated())
        return;

    scrobbleSent = true;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.queueScrobble(current, playbackTime, startWallclock, /*refreshOnSubmit=*/true);
}

void LastfmTracker::handleDynamicStreamUpdate(const file_info& info)
{
    if (!isPlaying || !currentHandle.is_valid())
        return;

    // Stream-only path. Library/local behavior never enters here.
    if (!isCurrentStream)
        return;

    const int mode = dynamicSourcesMode();
    if (mode == 0)
        return;

    std::string newArtist, newTitle, newAlbum;
    if (!extractStreamArtistTitle(info, newArtist, newTitle, newAlbum))
        return;

    // Generic filter: station branding etc.
    if (looksLikeStationTitle(newTitle))
    {
        LFM_DEBUG("Stream dynamic ignored (looksLikeStationTitle): " << newTitle.c_str());
        return;
    }

    // De-dupe dynamic metadata updates (foobar may call both dynamic callbacks for the same change).
    // Keyed by stream URL + artist + title so the same track on another station still passes.
    const char* p = currentHandle->get_path();
    const std::string path = p ? p : "";

    if (path != dedupLastPath_)
    {
        dedupLastPath_ = path;
        dedupLastArtist_.clear();
        dedupLastTitle_.clear();
    }

    if (newArtist == dedupLastArtist_ && newTitle == dedupLastTitle_)
        return;

    dedupLastArtist_ = newArtist;
    dedupLastTitle_ = newTitle;

    if (newArtist == current.artist && newTitle == current.title && newAlbum == current.album)
        return;

    LastfmTrackInfo evaluated;
    evaluated.artist = newArtist;
    evaluated.title = newTitle;
    evaluated.album = newAlbum;

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(newArtist, newTitle, newAlbum) ||
        lastfm::exclusion_filters::isExcludedByTitleFormattingFilter(currentHandle, evaluated, &info))
    {
        LFM_DEBUG("Stream dynamic ignored: excluded by filters.");
        return;
    }

    // Only do scrobble-related work in mode 2.
    if (mode == 2)
    {
        // We are about to switch to a new stream "track" (dynamic metadata change).
        // If the previous segment has already reached >=30s, it should be cached.
        maybeCacheDynamicScrobble();
        // Submit the previous segment (if cached) BEFORE sending NP for the new one.
        submitDynamicPendingIfAny(); // this should queue + retryAsync() internally
    }

    current.artist = newArtist;
    current.title = newTitle;
    current.album = newAlbum;

    // Start a new dynamic segment from this point.
    startDynamicSegment();

    if (lastfmIsSuspended())
        return;

    auto& scrobbler = LastfmCore::instance().scrobbler();

    // If we were waiting for dynamic metadata, this is the "start" of the stream track.
    if (pendingDueToMissingMetadata)
    {
        pendingDueToMissingMetadata = false;

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

void LastfmTracker::startDynamicSegment()
{
    dynamicActive = true;
    dynamicPending = false;
    dynamicSubmitted = false;
    dynamicSegmentStartWallclock = std::time(nullptr);

    effectiveListenedSeconds = 0.0;
    haveLastReportedTime = false;
}

void LastfmTracker::resetDynamicSegmentState()
{
    dynamicActive = false;
    dynamicPending = false;
    dynamicSubmitted = false;

    dynamicPendingTrack = LastfmTrackInfo{};
    dynamicPendingPlaybackTime = 0.0;
    dynamicPendingStartWallclock = 0;

    dynamicSegmentStartWallclock = 0;
    dedupLastPath_.clear();
    dedupLastArtist_.clear();
    dedupLastTitle_.clear();
}

void LastfmTracker::maybeCacheDynamicScrobble()
{
    // Only cache when dynamic scrobbling is enabled (mode 2).
    if (dynamicSourcesMode() != lastfm::settings::DynamicSourcesNowPlayingAndScrobbling)
        return;

    if (!currentHandle.is_valid() || !isCurrentStream)
        return;

    if (!dynamicActive || dynamicPending || dynamicSubmitted)
        return;

    if (current.artist.empty() || current.title.empty())
        return;

    // Cache exactly once when effective listening reaches 30s.
    if (effectiveListenedSeconds < 30.0)
        return;

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(current.artist, current.title, current.album))
        return;

    dynamicPending = true;
    dynamicPendingTrack = current;
    dynamicPendingPlaybackTime = playbackTime;
    dynamicPendingStartWallclock = dynamicSegmentStartWallclock;

    LFM_DEBUG("Stream scrobble cached: " << current.artist.c_str() << " - " << current.title.c_str());
}

void LastfmTracker::submitDynamicPendingIfAny()
{
    if (!dynamicPending || dynamicSubmitted)
        return;

    if (!currentHandle.is_valid() || !isCurrentStream)
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

    if (lastfm::exclusion_filters::isExcludedByTextOrRegexFilters(dynamicPendingTrack.artist, dynamicPendingTrack.title,
                                                                  dynamicPendingTrack.album))
    {
        dynamicSubmitted = true;
        dynamicPending = false;
        return;
    }

    dynamicSubmitted = true;
    dynamicPending = false;

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.queueScrobble(dynamicPendingTrack, dynamicPendingPlaybackTime, dynamicPendingStartWallclock,
                            /*refreshOnSubmit=*/true);
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
    refreshCurrentFileMetadata(true);
}
void LastfmTracker::on_volume_change(float)
{
}

static play_callback_static_factory_t<LastfmTracker> lastfmTrackerFactory;
