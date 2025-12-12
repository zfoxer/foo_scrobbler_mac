//
//  lastfm_auth.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_auth.h"
#include "lastfm_no.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <string>
#include <map>

#include <CommonCrypto/CommonDigest.h> // MD5

namespace
{
static std::string lastfmPendingToken;
}

bool hasPendingToken()
{
    return !lastfmPendingToken.empty();
}

// MD5 lowercase hex via Apple CommonCrypto
static std::string md5Hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);

    const char hexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32); // 16 bytes -> 32 hex chars

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        const unsigned char b = digest[i];
        out.push_back(hexDigits[b >> 4]);
        out.push_back(hexDigits[b & 0x0F]);
    }

    return out;
}

// Run GET URL and read entire body into pfc::string8
static bool httpGetToString(const char* url, pfc::string8& outBody)
{
    try
    {
        auto request = http_client::get()->create_request("GET");
        file::ptr stream = request->run(url, fb2k::noAbort);

        pfc::string8 line;
        outBody.reset();

        // Most JSON responses from Last.fm are a single line, reading all lines just in case.
        while (!stream->is_eof(fb2k::noAbort))
        {
            line.reset();
            stream->read_string_raw(line, fb2k::noAbort);
            outBody += line;
        }

        return true;
    }
    catch (std::exception const& e)
    {
        LFM_INFO("HTTP GET failed: " << e.what());
        return false;
    }
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

    // Build params for auth.getToken
    std::map<std::string, std::string> params = {
        {"api_key", apiKey},
        {"method", "auth.getToken"},
    };

    // Build api_sig = md5(concat(sorted params) + secret)
    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += apiSecret;
    sig = md5Hex(sig);

    // Build URL
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getToken"
        << "&api_key=" << apiKey.c_str() << "&api_sig=" << sig.c_str() << "&format=json";

    pfc::string8 body;
    if (!httpGetToString(url, body))
    {
        LFM_INFO("auth.getToken request failed.");
        return false;
    }

    // JSON parse: look for "token":"..."
    const char* p = strstr(body.c_str(), "\"token\":\"");
    if (!p)
    {
        LFM_INFO("auth.getToken: token not found. Response: " << body.c_str());
        return false;
    }
    p += 9; // Skip "token":"
    const char* end = strchr(p, '"');
    if (!end)
    {
        LFM_INFO("auth.getToken: malformed token field.");
        return false;
    }

    lastfmPendingToken.assign(p, end - p);
    LFM_DEBUG("Received auth token: " << lastfmPendingToken.c_str());

    // Browser URL for user authorization
    outAuthUrl = "https://www.last.fm/api/auth/"
                 "?api_key=" +
                 apiKey + "&token=" + lastfmPendingToken;

    return true;
}

// Using the stored token, call auth.getSession
bool completeAuthFromCallbackUrl(const std::string& callbackUrl, LastfmAuthState& authState)
{
    (void)callbackUrl; // unused for this flow

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

    // Build params for auth.getSession
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
    sig = md5Hex(sig);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getSession"
        << "&api_key=" << apiKey.c_str() << "&api_sig=" << sig.c_str() << "&token=" << lastfmPendingToken.c_str()
        << "&format=json";

    pfc::string8 body;
    if (!httpGetToString(url, body))
    {
        LFM_INFO("auth.getSession request failed.");
        return false;
    }

    // Parse `"name":"USERNAME"` and `"key":"SESSION_KEY"`
    const char* p = strstr(body.c_str(), "\"name\":\"");
    if (!p)
    {
        LFM_INFO("auth.getSession: username not found. Response: " << body.c_str());
        return false;
    }
    p += 8;
    const char* end = strchr(p, '"');
    if (!end)
    {
        LFM_INFO("auth.getSession: malformed name field.");
        return false;
    }
    authState.username.assign(p, end - p);

    p = strstr(end, "\"key\":\"");
    if (!p)
    {
        LFM_INFO("auth.getSession: session key not found.");
        return false;
    }
    p += 7;
    const char* end2 = strchr(p, '"');
    if (!end2)
    {
        LFM_INFO("auth.getSession: malformed key field.");
        return false;
    }
    authState.sessionKey.assign(p, end2 - p);
    authState.isAuthenticated = true;
    setAuthState(authState);

    LFM_DEBUG("Authentication complete. User: " << authState.username.c_str());
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
