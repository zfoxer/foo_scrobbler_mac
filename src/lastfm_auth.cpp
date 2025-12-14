// lastfm_auth.cpp
// foo_scrobbler_mac
//
// (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_auth.h"
#include "lastfm_no.h"
#include "lastfm_util.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <map>
#include <string>

namespace
{
static std::string lastfmPendingToken;
}

bool hasPendingToken()
{
    return !lastfmPendingToken.empty();
}

// Get token & browser URL
bool beginAuth(std::string& outAuthUrl)
{
    outAuthUrl.clear();
    lastfmPendingToken.clear();

    const std::string apiKey = __s66_x3();
    const std::string apiSecret = __s64_x9();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("API key/secret not configured.");
        return false;
    }

    std::map<std::string, std::string> params = {
        {"api_key", apiKey},
        {"method", "auth.getToken"},
    };

    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += apiSecret;
    sig = lastfm::util::md5HexLower(sig);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getToken"
        << "&api_key=" << apiKey.c_str() << "&api_sig=" << sig.c_str() << "&format=json";

    pfc::string8 body;
    std::string httpError;
    if (!lastfm::util::httpGetToString(url, body, httpError))
    {
        LFM_INFO("auth.getToken request failed: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return false;
    }

    std::string token;
    if (!lastfm::util::jsonFindStringValue(body.c_str(), "token", token) || token.empty())
    {
        LFM_INFO("auth.getToken: token not found. (response omitted, size=" << body.get_length() << ")");
        return false;
    }

    lastfmPendingToken = token;

    outAuthUrl = "https://www.last.fm/api/auth/"
                 "?api_key=" +
                 apiKey + "&token=" + lastfmPendingToken;

    return true;
}

// Using the stored token, call auth.getSession
bool completeAuthFromCallbackUrl(const std::string& callbackUrl, LastfmAuthState& authState)
{
    (void)callbackUrl;

    if (lastfmPendingToken.empty())
    {
        LFM_DEBUG("No pending token. Run beginAuth() first.");
        return false;
    }

    const std::string apiKey = __s66_x3();
    const std::string apiSecret = __s64_x9();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("API key/secret not configured.");
        return false;
    }

    std::map<std::string, std::string> params = {
        {"api_key", apiKey},
        {"method", "auth.getSession"},
        {"token", lastfmPendingToken},
    };

    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += apiSecret;
    sig = lastfm::util::md5HexLower(sig);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getSession"
        << "&api_key=" << apiKey.c_str() << "&api_sig=" << sig.c_str() << "&token=" << lastfmPendingToken.c_str()
        << "&format=json";

    pfc::string8 body;
    std::string httpError;
    if (!lastfm::util::httpGetToString(url, body, httpError))
    {
        LFM_INFO("auth.getSession request failed: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return false;
    }

    std::string name;
    std::string key;

    if (!lastfm::util::jsonFindStringValue(body.c_str(), "name", name) || name.empty())
    {
        LFM_INFO("auth.getSession: username not found. (response omitted, size=" << body.get_length() << ")");
        return false;
    }
    if (!lastfm::util::jsonFindStringValue(body.c_str(), "key", key) || key.empty())
    {
        LFM_INFO("auth.getSession: session key not found. (response omitted, size=" << body.get_length() << ")");
        return false;
    }

    authState.username = name;
    authState.sessionKey = key;
    authState.isAuthenticated = true;

    setAuthState(authState);

    LFM_INFO("Authentication complete. User: " << authState.username.c_str());
    lastfmPendingToken.clear();
    return true;
}

void logout()
{
    LFM_INFO("Clearing stored Last.fm session.");
    LastfmAuthState state;
    state.isAuthenticated = false;
    state.username.clear();
    state.sessionKey.clear();
    setAuthState(state);
}
