//
//  lastfm_nowplaying.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_nowplaying.h"
#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <CommonCrypto/CommonDigest.h>

#include <map>
#include <string>
#include <cctype>
#include <cstring>

namespace
{

// Local MD5 -> lowercase hex
static std::string md5Hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);
    const char hexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(CC_MD5_DIGEST_LENGTH * 2);

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        const unsigned char b = digest[i];
        out.push_back(hexDigits[b >> 4]);
        out.push_back(hexDigits[b & 0x0F]);
    }

    return out;
}

// URL-encode a value for query string.
static std::string urlEncode(const std::string& value)
{
    std::string out;
    out.reserve(value.size() * 3);

    const char* hex = "0123456789ABCDEF";

    for (unsigned char c : value)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }

    return out;
}

// Run HTTP request with explicit method ("POST")
// and read entire body into pfc::string8.
static bool httpRequestToString(const char* method, const char* url, pfc::string8& outBody, std::string& outError)
{
    try
    {
        auto req = http_client::get()->create_request(method);
        file::ptr stream = req->run(url, fb2k::noAbort);

        pfc::string8 line;
        outBody.reset();

        while (!stream->is_eof(fb2k::noAbort))
        {
            line.reset();
            stream->read_string_raw(line, fb2k::noAbort);
            outBody += line;
        }

        outError.clear();
        return true;
    }
    catch (std::exception const& e)
    {
        const char* what = e.what();
        outError = what ? what : "";
        return false;
    }
}

static bool responseHasError(const char* body)
{
    return body && std::strstr(body, "\"error\"");
}

static bool responseIsInvalidSession(const char* body)
{
    if (!body)
        return false;

    if (std::strstr(body, "Invalid session key"))
        return true;
    if (std::strstr(body, "\"error\":9"))
        return true;

    return false;
}

} // anonymous namespace

bool sendNowPlaying(const std::string& artist, const std::string& title, const std::string& album,
                    double durationSeconds)
{
    // Check auth state.
    LastfmAuthState state = getAuthState();
    if (!state.isAuthenticated || state.sessionKey.empty())
    {
        LFM_INFO("NowPlaying: not authenticated, skipping.");
        return false;
    }

    // Reject malformed tracks with missing mandatory metadata.
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

    // Build params (no encoding here; map sorts keys for signature).
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

    // Build signature: concat key+value in key order, append secret, MD5.
    std::string sigSrc;
    for (const auto& kv : params)
    {
        sigSrc += kv.first;
        sigSrc += kv.second;
    }
    sigSrc += apiSecret;

    const std::string apiSig = md5Hex(sigSrc);

    // Build URL with encoded values.
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encodedVal = urlEncode(kv.second);
        url << kv.first.c_str() << "=" << encodedVal.c_str();
    }

    url << "&api_sig=" << apiSig.c_str() << "&format=json";

    LFM_DEBUG("NowPlaying URL: " << url);
    pfc::string8 body;
    std::string httpError;

    if (!httpRequestToString("POST", url.c_str(), body, httpError))
    {
        LFM_INFO("" << "NowPlaying: HTTP request failed: "
                    << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return false;
    }

    const char* bodyC = body.c_str();

    {
        LFM_DEBUG("NowPlaying response: " << (bodyC ? bodyC : "(null)"));
    }

    if (!responseHasError(bodyC))
    {
        LFM_DEBUG("NowPlaying OK.");
        return true;
    }

    if (responseIsInvalidSession(bodyC))
    {
        LFM_INFO("NowPlaying: invalid session key (ignored here, scrobble path will clear auth).");
        return false;
    }

    LFM_INFO("NowPlaying: API error.");
    return false;
}
