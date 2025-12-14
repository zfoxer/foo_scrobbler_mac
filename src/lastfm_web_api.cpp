// lastfm_web_api.cpp
// foo_scrobbler_mac
//
// (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_web_api.h"

#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "lastfm_util.h"
#include "debug.h"
#include "lastfm_nowplaying.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>

namespace
{
static bool iContains(const std::string& haystack, const char* needle)
{
    if (!needle || !*needle)
        return false;

    auto lower = [](unsigned char c) { return (unsigned char)std::tolower(c); };

    std::string h;
    h.reserve(haystack.size());
    for (unsigned char c : haystack)
        h.push_back((char)lower(c));

    std::string n;
    n.reserve(std::strlen(needle));
    for (const char* p = needle; *p; ++p)
        n.push_back((char)lower((unsigned char)*p));

    return h.find(n) != std::string::npos;
}
} // namespace

bool LastfmWebApi::updateNowPlaying(const LastfmTrackInfo& track)
{
    return sendNowPlaying(track.artist, track.title, track.album, track.durationSeconds);
}

LastfmScrobbleResult LastfmWebApi::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp)
{
    LastfmAuthState authState = getAuthState();
    if (!authState.isAuthenticated || authState.sessionKey.empty())
    {
        LFM_INFO("LastfmWebApi::scrobble(): no valid auth state.");
        return LastfmScrobbleResult::INVALID_SESSION;
    }

    const std::string apiKey = __s66_x3();
    const std::string apiSecret = __s64_x9();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("LastfmWebApi::scrobble(): API key/secret not configured.");
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    std::time_t startTs = 0;
    if (startTimestamp > 0)
    {
        startTs = startTimestamp;
    }
    else
    {
        std::time_t now = std::time(nullptr);
        if (now <= 0)
            now = 0;
        startTs = now - static_cast<std::time_t>(playbackSeconds);
    }

    std::map<std::string, std::string> params = {
        {"api_key", apiKey},          {"artist", track.artist},
        {"track", track.title},       {"timestamp", std::to_string(static_cast<long long>(startTs))},
        {"method", "track.scrobble"}, {"sk", authState.sessionKey},
    };

    if (!track.album.empty())
        params["album"] = track.album;
    if (track.durationSeconds > 0.0)
        params["duration"] = std::to_string(static_cast<int>(track.durationSeconds));

    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += apiSecret;
    sig = lastfm::util::md5HexLower(sig);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encodedValue = lastfm::util::urlEncode(kv.second);
        url << kv.first.c_str() << "=" << encodedValue.c_str();
    }

    url << "&api_sig=" << sig.c_str() << "&format=json";

    pfc::string8 body;
    std::string httpError;

    if (!lastfm::util::httpPostToString(url.c_str(), body, httpError))
    {
        if (!httpError.empty() && (iContains(httpError, "403") || iContains(httpError, "forbidden")))
        {
            LFM_INFO("Scrobble: HTTP 403 / Forbidden (" << httpError.c_str() << "). Treating as invalid session.");
            return LastfmScrobbleResult::INVALID_SESSION;
        }

        LFM_INFO("Scrobble: HTTP request failed: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return LastfmScrobbleResult::TEMPORARY_ERROR;
    }

    const char* bodyC = body.c_str();
    LFM_DEBUG("Scrobble response received. (size=" << (bodyC ? strlen(bodyC) : 0) << ")");

    if (!lastfm::util::jsonHasKey(bodyC, "error"))
    {
        LFM_DEBUG("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
        return LastfmScrobbleResult::SUCCESS;
    }

    int errCode = 0;
    if (lastfm::util::jsonFindIntValue(bodyC, "error", errCode))
    {
        if (errCode == 9)
        {
            LFM_INFO("Scrobble failed: Invalid session key (API error 9).");
            return LastfmScrobbleResult::INVALID_SESSION;
        }

        if (errCode == 11 || errCode == 16)
        {
            LFM_INFO("Scrobble failed: Temporary Last.fm API error (" << errCode << ").");
            return LastfmScrobbleResult::TEMPORARY_ERROR;
        }
    }

    LFM_INFO("Scrobble failed: API error.");
    return LastfmScrobbleResult::OTHER_ERROR;
}
