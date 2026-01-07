//
//  lastfm_nowplaying.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_nowplaying.h"
#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "lastfm_util.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <map>
#include <string>
#include <cstring>

bool sendNowPlaying(const std::string& artist, const std::string& title, const std::string& album,
                    double durationSeconds)
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

    const std::string apiKey = __s66_x3();
    const std::string apiSecret = __s64_x9();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("NowPlaying: API key/secret not configured.");
        return false;
    }

    std::map<std::string, std::string> params = {
        {"api_key", apiKey},      {"artist", artist}, {"track", title}, {"method", "track.updateNowPlaying"},
        {"sk", state.sessionKey},
    };

    if (!album.empty())
        params["album"] = album;

    if (durationSeconds > 0.0)
    {
        int dur = static_cast<int>(durationSeconds + 0.5);
        params["duration"] = std::to_string(dur);
    }

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

    pfc::string8 body;
    std::string httpError;

    if (!lastfm::util::httpPostToString(url.c_str(), body, httpError))
    {
        LFM_INFO("NowPlaying: HTTP request failed: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return false;
    }

    const char* bodyC = body.c_str();
    LFM_DEBUG("NowPlaying response received. (size=" << body.get_length() << ")");

    lastfm::util::LastfmApiErrorInfo apiInfo = lastfm::util::extractLastfmApiError(bodyC);

    if (!apiInfo.hasJson)
    {
        LFM_INFO("NowPlaying: response is not valid JSON (size=" << body.get_length() << ")");
        return false;
    }

    if (!apiInfo.hasError)
    {
        LFM_DEBUG("NowPlaying OK.");
        return true;
    }

    if (apiInfo.errorCode == 9)
    {
        LFM_INFO("NowPlaying: invalid session key (ignored here, scrobble path will clear auth).");
        return false;
    }

    LFM_INFO("NowPlaying: API error " << apiInfo.errorCode << (apiInfo.message.empty() ? "" : ": ")
                                      << apiInfo.message.c_str());
    return false;
}
