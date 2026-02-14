//
//  lastfm_tracker.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_tracker.h"
#include "lastfm_core.h"
#include "lastfm_state.h"
#include "debug.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <cstring>

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

    bool hasAlnum = false;
    for (unsigned char c : s)
    {
        if (std::isalnum(c))
        {
            hasAlnum = true;
            break;
        }
    }

    if (!hasAlnum)
        return {};

    return s;
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

        artist = cleanTagValue(left.c_str());
        title = cleanTagValue(right.c_str());

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

    auto get1 = [&](const char* key) -> std::string { return cleanTagValue(info.meta_get(key, 0)); };

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

static std::string getTagCase(const file_info& info, const char* lower, const char* upper)
{
    std::string v = cleanTagValue(info.meta_get(lower, 0));
    if (!v.empty())
        return v;
    return cleanTagValue(info.meta_get(upper, 0));
}

static std::string getTagAny(const file_info& info, std::initializer_list<const char*> keys)
{
    for (const char* k : keys)
    {
        std::string v = cleanTagValue(info.meta_get(k, 0));
        if (!v.empty())
            return v;
    }
    return {};
}

// Map your radio choice -> actual tag fetch.
static std::string getArtistByChoice(const file_info& info)
{
    switch (lastfmTagArtistSource())
    {
    case 1:
        return getTagAny(
            info, {"album artist", "ALBUM ARTIST", "album_artist", "ALBUM_ARTIST", "albumartist", "ALBUMARTIST"});
    case 2:
        return getTagAny(info, {"performer", "PERFORMER"});
    case 3:
        return getTagAny(info, {"composer", "COMPOSER"});
    case 4:
        return getTagAny(info, {"conductor", "CONDUCTOR"});
    case 0:
    default:
        return getTagCase(info, "artist", "ARTIST");
    }
}

static std::string getAlbumArtistByChoice(const file_info& info)
{
    switch (lastfmTagAlbumArtistSource())
    {
    case 1:
        return getTagCase(info, "artist", "ARTIST");
    case 2:
        return getTagAny(info, {"performer", "PERFORMER"});
    case 3:
        return getTagAny(info, {"composer", "COMPOSER"});
    case 4:
        return getTagAny(info, {"conductor", "CONDUCTOR"});
    case 0:
    default:
        return getTagAny(
            info, {"album artist", "ALBUM ARTIST", "album_artist", "ALBUM_ARTIST", "albumartist", "ALBUMARTIST"});
    }
}

static std::string getTitleByChoice(const file_info& info)
{
    switch (lastfmTagTitleSource())
    {
    case 1:
        return getTagAny(info, {"work", "WORK"});
    case 2:
        return getTagAny(info, {"movement", "MOVEMENT"});
    case 3:
        return getTagAny(info, {"part", "PART"});
    case 4:
        return getTagAny(info, {"subtitle", "SUBTITLE"});
    case 0:
    default:
        return getTagCase(info, "title", "TITLE");
    }
}

static std::string getAlbumByChoice(const file_info& info)
{
    switch (lastfmTagAlbumSource())
    {
    case 1:
        return getTagAny(info, {"release", "RELEASE"});
    case 2:
        return getTagAny(info, {"work", "WORK"});
    case 3:
        return getTagAny(info, {"albumtitle", "ALBUMTITLE"});
    case 4:
        return getTagAny(info, {"discname", "DISCNAME"});
    case 0:
    default:
        return getTagCase(info, "album", "ALBUM");
    }
}

static void applyFallbackIfEnabled(std::string& artist, std::string& album, const file_info& info)
{
    if (!lastfmTagFallbackArtistAlbum())
        return;

    // Only fallback when user selected something non-default and it came back empty.
    if (artist.empty() && lastfmTagArtistSource() != 0)
        artist = getTagCase(info, "artist", "ARTIST");

    if (album.empty() && lastfmTagAlbumSource() != 0)
        album = getTagCase(info, "album", "ALBUM");
}

static void applyVariousArtistsRule(std::string& albumArtist)
{
    if (!lastfmTagTreatVariousArtistsAsEmpty())
        return;

    if (albumArtist.empty())
        return;

    std::string s = albumArtist;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    if (s == "various artists")
        albumArtist.clear();
}
} // namespace

unsigned LastfmTracker::get_flags()
{
    return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_time | flag_on_playback_seek |
           flag_on_playback_pause | flag_on_playback_dynamic_info | flag_on_playback_dynamic_info_track;
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

    resetDynamicSegmentState();
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

    current.artist = getArtistByChoice(info);
    current.title = getTitleByChoice(info);
    current.album = getAlbumByChoice(info);
    current.albumArtist = getAlbumArtistByChoice(info);

    applyFallbackIfEnabled(current.artist, current.album, info);
    applyVariousArtistsRule(current.albumArtist);

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
    isCurrentStream = isNetworkStreamPath(track);
    LFM_DEBUG("Track path: " << (track->get_path() ? track->get_path() : "<null>")
                             << " stream=" << (isCurrentStream ? "yes" : "no"));

    // Natural boundary: submit previous track (if eligible) before switching state.
    submitDynamicPendingIfAny();
    submitScrobbleIfNeeded();
    LastfmCore::instance().scrobbler().retryAsync();

    resetState();
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

    if (lastfmOnlyScrobbleFromMediaLibrary() && !isTrackInMediaLibrary(track))
    {
        LFM_DEBUG("Track skipped: not in Media Library.");
        resetState();
        return;
    }

    if (lastfmIsSuspended())
        return;

    LFM_DEBUG("Now playing: " << current.artist.c_str() << " - " << current.title.c_str());

    auto& scrobbler = LastfmCore::instance().scrobbler();
    scrobbler.onNowPlaying(current);
}

void LastfmTracker::on_playback_time(double time)
{
    playbackTime = time;

    const bool suspended = lastfmIsSuspended();

    // Policy: while suspended, freeze scrobble progress (do not count time).
    if (!suspended)
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

    auto& scrobbler = LastfmCore::instance().scrobbler();
    if (currentHandle.is_valid() && (scrobbleSent || pendingDueToMissingMetadata) && !isCurrentStream)
    {
        file_info_impl info;
        if (currentHandle->get_info(info))
        {
            std::string newArtist = getArtistByChoice(info);
            std::string newTitle = getTitleByChoice(info);
            std::string newAlbum = getAlbumByChoice(info);
            std::string newAlbumArtist = getAlbumArtistByChoice(info);

            applyFallbackIfEnabled(newArtist, newAlbum, info);
            applyVariousArtistsRule(newAlbumArtist);

            if (newArtist != current.artist || newTitle != current.title || newAlbum != current.album ||
                newAlbumArtist != current.albumArtist)
            {
                current.artist = newArtist;
                current.title = newTitle;
                current.album = newAlbum;
                current.albumArtist = newAlbumArtist;

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

    // Stream-only: cache a dynamic scrobble payload once we have >=30s effective listening.
    maybeCacheDynamicScrobble();

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
    if (lastfmOnlyScrobbleFromMediaLibrary() && currentHandle.is_valid())
    {
        if (!isTrackInMediaLibrary(currentHandle))
            return;
    }

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
            current.artist = getArtistByChoice(info);
            current.title = getTitleByChoice(info);
            current.album = getAlbumByChoice(info);
            current.albumArtist = getAlbumArtistByChoice(info);

            applyFallbackIfEnabled(current.artist, current.album, info);
            applyVariousArtistsRule(current.albumArtist);
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
    if (lastfmIsSuspended())
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

    const int mode = lastfmDynamicSourcesMode();
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

    if (newArtist == current.artist && newTitle == current.title && newAlbum == current.album)
        return;

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
    dynamicActive = true;
    dynamicPending = false;
    dynamicSubmitted = false;
    dynamicSegmentStartWallclock = std::time(nullptr);

    effectiveListenedSeconds = 0.0;
    haveLastReportedTime = false;

    if (lastfmIsSuspended())
        return;

    auto& scrobbler = LastfmCore::instance().scrobbler();

    // If we were waiting for dynamic metadata, this is the "start" of the stream track.
    if (pendingDueToMissingMetadata)
    {
        pendingDueToMissingMetadata = false;

        // Start dynamic segment from first valid metadata.
        dynamicActive = true;
        dynamicPending = false;
        dynamicSubmitted = false;
        dynamicSegmentStartWallclock = std::time(nullptr);

        effectiveListenedSeconds = 0.0;
        haveLastReportedTime = false;

        if (lastfmDisableNowPlaying())
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
    if (lastfmDisableNowPlaying())
    {
        LFM_DEBUG("Dynamic NP suppressed (dynamic): " << current.artist.c_str() << " - " << current.title.c_str());
    }
    else
    {
        LFM_DEBUG("Submitting dynamic NP (dynamic): " << current.artist.c_str() << " - " << current.title.c_str());
        scrobbler.sendNowPlayingOnly(current);
    }
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
}

void LastfmTracker::maybeCacheDynamicScrobble()
{
    // Only cache when dynamic scrobbling is enabled (mode 2).
    if (lastfmDynamicSourcesMode() != 2)
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

    if (lastfmDynamicSourcesMode() != 2)
        return;

    // Keep global policy consistent. If user selected "only from Media Library", streams never scrobble.
    if (lastfmOnlyScrobbleFromMediaLibrary())
        return;

    // Do not submit while suspended; keep it cached for the next boundary after resume.
    if (lastfmIsSuspended())
        return;

    if (!lastfmIsAuthenticated())
        return;

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

// Unused callbacks (required by interface)
void LastfmTracker::on_playback_starting(play_control::t_track_command, bool)
{
}
void LastfmTracker::on_playback_edited(metadb_handle_ptr)
{
}
void LastfmTracker::on_volume_change(float)
{
}

static play_callback_static_factory_t<LastfmTracker> lastfmTrackerFactory;
