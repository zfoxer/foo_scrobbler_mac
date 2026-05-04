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
#include <vector>
#include <cassert>

namespace
{
using ApiParams = std::map<std::string, std::string>;

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

        case 8:
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

static std::string buildApiSignature(const ApiParams& params, const std::string& apiSecret)
{
    std::string sigSrc;
    for (const auto& kv : params)
    {
        sigSrc += kv.first;
        sigSrc += kv.second;
    }
    sigSrc += apiSecret;

    return lastfm::util::md5HexLower(sigSrc);
}

static std::string buildSignedFormBody(const ApiParams& params, const std::string& apiSecret)
{
    const std::string apiSig = buildApiSignature(params, apiSecret);

    std::string body;
    bool first = true;
    auto append = [&](const std::string& key, const std::string& value)
    {
        if (!first)
            body += "&";
        first = false;

        body += lastfm::util::urlEncode(key);
        body += "=";
        body += lastfm::util::urlEncode(value);
    };

    for (const auto& kv : params)
        append(kv.first, kv.second);

    append("api_sig", apiSig);
    append("format", "json");
    return body;
}

static std::time_t resolveStartTimestamp(const LastfmScrobbleRequest& request, std::time_t now)
{
    if (request.startTimestamp > 0)
        return request.startTimestamp;

    if (now <= 0)
        now = 0;

    return now - static_cast<std::time_t>(request.playbackSeconds);
}

static bool appendIndexedScrobbleParams(ApiParams& params, const LastfmScrobbleRequest& request, std::size_t index,
                                        std::time_t now)
{
    const LastfmTrackInfo& track = request.track;
    if (track.artist.empty() || track.title.empty())
        return false;

    const std::string suffix = "[" + std::to_string(index) + "]";
    params["artist" + suffix] = track.artist;
    params["track" + suffix] = track.title;
    params["timestamp" + suffix] = std::to_string(static_cast<long long>(resolveStartTimestamp(request, now)));

    if (!track.album.empty())
        params["album" + suffix] = track.album;
    if (!track.albumArtist.empty())
        params["albumArtist" + suffix] = track.albumArtist;
    if (!track.mbid.empty())
        params["mbid" + suffix] = track.mbid;
    if (track.durationSeconds > 0.0)
        params["duration" + suffix] = std::to_string(static_cast<int>(track.durationSeconds));

    return true;
}

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

static bool postNowPlayingAndClassify(const std::string& formBody)
{
    pfc::string8 body;
    std::string httpError;

    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", formBody, body, httpError);

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

    const std::string formBody = buildSignedFormBody(params, apiSecret);
    return postNowPlayingAndClassify(formBody);
}

LastfmScrobbleResult LastfmWebApi::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp)
{
#ifdef LFM_DEBUG
    static bool tested = (selfTest_extractLastfmApiError(), true);
    (void)tested;
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

    ApiParams params = {
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

    pfc::string8 body;
    std::string httpError;

    const std::string formBody = buildSignedFormBody(params, apiSecret);
    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", formBody, body, httpError);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
    }

    return outcome.result;
}

LastfmScrobbleResult LastfmWebApi::scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests)
{
#ifdef LFM_DEBUG
    static bool tested = (selfTest_extractLastfmApiError(), true);
    (void)tested;
#endif

    if (requests.empty())
        return LastfmScrobbleResult::SUCCESS;

    if (requests.size() > 50)
    {
        LFM_INFO("LastfmWebApi::scrobbleBatch(): batch too large: " << requests.size());
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    LastfmAuthState authState = getAuthState();
    if (!authState.isAuthenticated || authState.sessionKey.empty())
    {
        LFM_INFO("LastfmWebApi::scrobbleBatch(): no valid auth state.");
        return LastfmScrobbleResult::INVALID_SESSION;
    }

    const std::string apiKey = __key();
    const std::string apiSecret = __sec();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("LastfmWebApi::scrobbleBatch(): API key/secret not configured.");
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    ApiParams params = {
        {"api_key", apiKey},
        {"method", "track.scrobble"},
        {"sk", authState.sessionKey},
    };

    const std::time_t now = std::time(nullptr);
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        if (!appendIndexedScrobbleParams(params, requests[i], i, now))
        {
            LFM_INFO("LastfmWebApi::scrobbleBatch(): missing artist/title at index " << (unsigned)i);
            return LastfmScrobbleResult::OTHER_ERROR;
        }
    }

    const std::string bodyText = buildSignedFormBody(params, apiSecret);

    pfc::string8 body;
    std::string httpError;

    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", bodyText, body, httpError);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        if (requests.size() == 1)
        {
            const LastfmTrackInfo& track = requests.front().track;
            LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
        }
        else
        {
            LFM_INFO("Scrobble batch OK: count=" << (unsigned)requests.size());
        }
    }
    else
        LFM_INFO("Scrobble batch failed: result=" << static_cast<int>(outcome.result)
                                                  << " count=" << (unsigned)requests.size());

    return outcome.result;
}
