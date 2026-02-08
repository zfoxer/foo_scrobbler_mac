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
#include "lastfm_nowplaying.h"
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
} // namespace

bool LastfmWebApi::updateNowPlaying(const LastfmTrackInfo& track)
{
    return sendNowPlaying(track.artist, track.title, track.album, track.durationSeconds);
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

    bool httpOk = lastfm::util::httpPostToString(url.c_str(), body, httpError);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
    }

    return outcome.result;
}
