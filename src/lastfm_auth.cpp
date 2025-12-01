//
//  lastfm_auth.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
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
static std::string g_lastfm_pending_token;
}

bool lastfm_has_pending_token()
{
    return !g_lastfm_pending_token.empty();
}

// MD5 lowercase hex via Apple CommonCrypto
static std::string md5_hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);

    const char hex_digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32); // 16 bytes -> 32 hex chars

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        const unsigned char b = digest[i];
        out.push_back(hex_digits[b >> 4]);
        out.push_back(hex_digits[b & 0x0F]);
    }

    return out;
}

// Run GET URL and read entire body into pfc::string8
static bool http_get_to_string(const char* url, pfc::string8& out_body)
{
    try
    {
        auto request = http_client::get()->create_request("GET");
        file::ptr stream = request->run(url, fb2k::noAbort);

        pfc::string8 line;
        out_body.reset();

        // Most JSON responses from Last.fm are a single line, but we read all lines just in case.
        while (!stream->is_eof(fb2k::noAbort))
        {
            line.reset();
            stream->read_string_raw(line, fb2k::noAbort);
            out_body += line;
        }

        return true;
    }
    catch (std::exception const& e)
    {
        LFM_INFO("HTTP GET failed: " << e.what());
        return false;
    }
}

// Step 1: Get token & browser URL
bool lastfm_begin_auth(std::string& out_auth_url)
{
    out_auth_url.clear();
    g_lastfm_pending_token.clear();

    const std::string api_key = __s66_x3();
    const std::string api_secret = __s64_x9();

    if (api_key.empty() || api_secret.empty())
    {
        LFM_INFO("API key/secret not configured.");
        return false;
    }

    // Build params for auth.getToken
    std::map<std::string, std::string> params = {
        {"api_key", api_key},
        {"method", "auth.getToken"},
    };

    // Build api_sig = md5(concat(sorted params) + secret)
    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += api_secret;
    sig = md5_hex(sig);

    // Build URL
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getToken"
        << "&api_key=" << api_key.c_str() << "&api_sig=" << sig.c_str() << "&format=json";

    pfc::string8 body;
    if (!http_get_to_string(url, body))
    {
        LFM_INFO("auth.getToken request failed.");
        return false;
    }

    // Simple JSON parse: look for "token":"..."
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

    g_lastfm_pending_token.assign(p, end - p);
    LFM_DEBUG("Received auth token: " << g_lastfm_pending_token.c_str());

    // Browser URL for user authorization
    out_auth_url = "https://www.last.fm/api/auth/"
                   "?api_key=" +
                   api_key + "&token=" + g_lastfm_pending_token;

    return true;
}

// Step 2: using the stored token, call auth.getSession
bool lastfm_complete_auth_from_callback_url(const std::string& callback_url, lastfm_auth_state& auth_state)
{
    (void)callback_url; // unused for this flow

    if (g_lastfm_pending_token.empty())
    {
        LFM_DEBUG("No pending token – run lastfm_begin_auth() first.");
        return false;
    }

    const std::string api_key = __s66_x3();
    const std::string api_secret = __s64_x9();

    if (api_key.empty() || api_secret.empty())
    {
        LFM_INFO("API key/secret not configured.");
        return false;
    }

    // Build params for auth.getSession
    std::map<std::string, std::string> params = {
        {"api_key", api_key},
        {"method", "auth.getSession"},
        {"token", g_lastfm_pending_token},
    };

    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += api_secret;
    sig = md5_hex(sig);

    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?method=auth.getSession"
        << "&api_key=" << api_key.c_str() << "&api_sig=" << sig.c_str() << "&token=" << g_lastfm_pending_token.c_str()
        << "&format=json";

    pfc::string8 body;
    if (!http_get_to_string(url, body))
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
    auth_state.username.assign(p, end - p);

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
    auth_state.session_key.assign(p, end2 - p);
    auth_state.is_authenticated = true;
    lastfm_set_auth_state(auth_state);
    LFM_DEBUG("Authentication complete. User: " << auth_state.username.c_str());
    g_lastfm_pending_token.clear();
    return true;
}

void lastfm_logout()
{
    LFM_INFO("Clearing stored Last.fm session.");
    lastfm_auth_state state;
    state.is_authenticated = false;
    state.username.clear();
    state.session_key.clear();
    lastfm_set_auth_state(state);
}
