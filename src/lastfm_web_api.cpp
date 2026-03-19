//
//  lastfm_web_api.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos.
//

#include "lastfm_web_api.h"
#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "lastfm_util.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <cassert>

namespace
{
struct ApiOutcome
{
    LastfmScrobbleResult result = LastfmScrobbleResult::OTHER_ERROR;
    int apiError = 0;
    std::string apiMessage;
    bool hasJson = false;
};

static ApiOutcome classifyResponse(bool httpOk, const std::string& httpError, const pfc::string8& body)
{
    ApiOutcome out;

    // Transport failure
    if (!httpOk)
    {
        LFM_INFO("Last.fm HTTP failure: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        out.result = LastfmScrobbleResult::TEMPORARY_ERROR;
        return out;
    }

    const char* bodyC = body.c_str();

    lastfm::util::LastfmApiErrorInfo apiInfo = lastfm::util::extractLastfmApiError(bodyC);

    if (!apiInfo.hasJson)
    {
        LFM_INFO("Last.fm response is not valid JSON (size=" << body.get_length() << ")");
        out.result = LastfmScrobbleResult::TEMPORARY_ERROR;
        return out;
    }

    out.hasJson = true;

    if (apiInfo.hasError)
    {
        out.apiError = apiInfo.errorCode;
        out.apiMessage = apiInfo.message;

        switch (apiInfo.errorCode)
        {
        case 9:
            out.result = LastfmScrobbleResult::INVALID_SESSION;
            break;

        case 11:
        case 16:
            out.result = LastfmScrobbleResult::TEMPORARY_ERROR;
            break;
        case 29:
            out.result = LastfmScrobbleResult::RATE_LIMITED;
            break;
        default:
            out.result = LastfmScrobbleResult::OTHER_ERROR;
            break;
        }

        LFM_INFO("Last.fm API error " << apiInfo.errorCode << (apiInfo.message.empty() ? "" : ": ")
                                      << apiInfo.message.c_str());
        return out;
    }

    // Success
    out.result = LastfmScrobbleResult::SUCCESS;
    return out;
}

#ifdef LFM_DEBUG

static void selfTest_extractLastfmApiError()
{
    {
        auto info = lastfm::util::extractLastfmApiError(nullptr);
        assert(!info.hasJson);
    }

    {
        auto info = lastfm::util::extractLastfmApiError("not json at all");
        assert(!info.hasJson);
    }

    {
        auto info = lastfm::util::extractLastfmApiError("{\"foo\":1}");
        assert(info.hasJson);
        assert(!info.hasError);
    }

    {
        auto info = lastfm::util::extractLastfmApiError("{\"error\":9,\"message\":\"Invalid session key\"}");
        assert(info.hasJson);
        assert(info.hasError);
        assert(info.errorCode == 9);
        assert(!info.message.empty());
    }
}

#endif

static bool buildNowPlayingParams(std::map<std::string, std::string>& params, std::string& apiSecretOut,
                                  const std::string& artist, const std::string& title, const std::string& album,
                                  const std::string& albumArtist, const std::string& mbid, double durationSeconds)
{
    LastfmAuthState state = getAuthState();
    if (!state.isAuthenticated || state.sessionKey.empty())
    {
        LFM_INFO("NowPlaying: not authenticated, skipping.");
        return false;
    }

    if (artist.empty() || title.empty())
    {
        LFM_INFO("Missing track info, not submitting.");
        return false;
    }

    const std::string apiKey = __key();
    const std::string apiSecret = __sec();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("NowPlaying: API key/secret not configured.");
        return false;
    }

    apiSecretOut = apiSecret;

    params = {
        {"api_key", apiKey},      {"artist", artist}, {"track", title}, {"method", "track.updateNowPlaying"},
        {"sk", state.sessionKey},
    };

    if (!album.empty())
        params["album"] = album;

    if (!albumArtist.empty())
        params["albumArtist"] = albumArtist;

    if (!mbid.empty())
        params["mbid"] = mbid;

    if (durationSeconds > 0.0)
    {
        int dur = static_cast<int>(durationSeconds + 0.5);
        params["duration"] = std::to_string(dur);
    }

    return true;
}

static pfc::string8 buildNowPlayingUrl(const std::map<std::string, std::string>& params, const std::string& apiSecret)
{
    std::string sigSrc;
    for (const auto& kv : params)
    {
        sigSrc += kv.first;
        sigSrc += kv.second;
    }
    sigSrc += apiSecret;

    const std::string apiSig = lastfm::util::md5HexLower(sigSrc);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encodedVal = lastfm::util::urlEncode(kv.second);
        url << kv.first.c_str() << "=" << encodedVal.c_str();
    }

    url << "&api_sig=" << apiSig.c_str() << "&format=json";
    return url;
}

static bool postNowPlayingAndClassify(const char* urlCStr)
{
    pfc::string8 body;
    std::string httpError;

    const bool httpOk = lastfm::util::httpPostToString(urlCStr, body, httpError);

    if (httpOk)
        LFM_DEBUG("NowPlaying response received. (size=" << body.get_length() << ")");

    const ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_DEBUG("NowPlaying OK.");
        return true;
    }

    return false;
}
} // namespace

bool LastfmWebApi::updateNowPlaying(const LastfmTrackInfo& track)
{
    std::map<std::string, std::string> params;
    std::string apiSecret;

    if (!buildNowPlayingParams(params, apiSecret, track.artist, track.title, track.album, track.albumArtist, track.mbid,
                               track.durationSeconds))
    {
        return false;
    }

    const pfc::string8 url = buildNowPlayingUrl(params, apiSecret);
    return postNowPlayingAndClassify(url.c_str());
}

LastfmScrobbleResult LastfmWebApi::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp)
{
#ifdef LFM_DEBUG
    static bool tested = (selfTest_extractLastfmApiError(), true);
#endif

    LastfmAuthState authState = getAuthState();
    if (!authState.isAuthenticated || authState.sessionKey.empty())
    {
        LFM_INFO("LastfmWebApi::scrobble(): no valid auth state.");
        return LastfmScrobbleResult::INVALID_SESSION;
    }

    const std::string apiKey = __key();
    const std::string apiSecret = __sec();

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
    if (!track.albumArtist.empty())
        params["albumArtist"] = track.albumArtist;
    if (!track.mbid.empty())
        params["mbid"] = track.mbid;
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

    bool httpOk = lastfm::util::httpPostToString(url.c_str(), body, httpError);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
    }

    return outcome.result;
}
