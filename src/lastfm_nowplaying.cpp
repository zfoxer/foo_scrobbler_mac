//
//  lastfm_nowplaying.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
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

// Local MD5 -> lowercase hex (same style as other modules).
static std::string md5_hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);

    const char hex_digits[] = "0123456789abcdef";

    std::string out;
    out.reserve(CC_MD5_DIGEST_LENGTH * 2);

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        const unsigned char b = digest[i];
        out.push_back(hex_digits[b >> 4]);
        out.push_back(hex_digits[b & 0x0F]);
    }

    return out;
}

// URL-encode a value for query string.
static std::string url_encode(const std::string& value)
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
static bool http_request_to_string(const char* method, const char* url, pfc::string8& out_body, std::string& out_error)
{
    try
    {
        auto req = http_client::get()->create_request(method);
        file::ptr stream = req->run(url, fb2k::noAbort);

        pfc::string8 line;
        out_body.reset();

        while (!stream->is_eof(fb2k::noAbort))
        {
            line.reset();
            stream->read_string_raw(line, fb2k::noAbort);
            out_body += line;
        }

        out_error.clear();
        return true;
    }
    catch (std::exception const& e)
    {
        const char* what = e.what();
        out_error = what ? what : "";
        return false;
    }
}

static bool response_has_error(const char* body)
{
    return body && std::strstr(body, "\"error\"");
}

static bool response_is_invalid_session(const char* body)
{
    if (!body)
        return false;

    if (std::strstr(body, "Invalid session key"))
        return true;
    if (std::strstr(body, "\"error\":9"))
        return true;

    return false;
}

} // namespace

bool lastfm_send_now_playing(const std::string& artist, const std::string& title, const std::string& album,
                             double duration_seconds)
{
    // Check auth state.
    lastfm_auth_state state = lastfm_get_auth_state();
    if (!state.is_authenticated || state.session_key.empty())
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

    const std::string api_key = __s66_x3();
    const std::string api_secret = __s64_x9();

    if (api_key.empty() || api_secret.empty())
    {
        LFM_INFO("NowPlaying: API key/secret not configured.");
        return false;
    }

    // Build params (no encoding here; map sorts keys for signature).
    std::map<std::string, std::string> params = {
        {"api_key", api_key},      {"artist", artist}, {"track", title}, {"method", "track.updateNowPlaying"},
        {"sk", state.session_key},
    };

    if (!album.empty())
        params["album"] = album;

    if (duration_seconds > 0.0)
    {
        int dur = static_cast<int>(duration_seconds + 0.5);
        params["duration"] = std::to_string(dur);
    }

    // Build signature: concat key+value in key order, append secret, MD5.
    std::string sig_src;
    for (const auto& kv : params)
    {
        sig_src += kv.first;
        sig_src += kv.second;
    }
    sig_src += api_secret;

    const std::string api_sig = md5_hex(sig_src);

    // Build URL with encoded values.
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encoded_val = url_encode(kv.second);
        url << kv.first.c_str() << "=" << encoded_val.c_str();
    }

    url << "&api_sig=" << api_sig.c_str() << "&format=json";

    LFM_DEBUG("NowPlaying URL: " << url);
    pfc::string8 body;
    std::string http_error;

    if (!http_request_to_string("POST", url.c_str(), body, http_error))
    {
        LFM_INFO("" << "NowPlaying: HTTP request failed: "
                    << (http_error.empty() ? "unknown error" : http_error.c_str()));
        return false;
    }

    const char* body_c = body.c_str();

    {
        LFM_DEBUG("NowPlaying response: " << (body_c ? body_c : "(null)"));
    }

    if (!response_has_error(body_c))
    {
        LFM_DEBUG("NowPlaying OK.");
        return true;
    }

    if (response_is_invalid_session(body_c))
    {
        LFM_INFO("NowPlaying: invalid session key (ignored here, scrobble path will clear auth).");
        return false;
    }

    LFM_INFO("NowPlaying: API error.");
    return false;
}
