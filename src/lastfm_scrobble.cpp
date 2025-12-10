//
//  lastfm_scrobble.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_scrobble.h"
#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "lastfm_tracker.h"
#include "lastfm_queue.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <CommonCrypto/CommonDigest.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <map>

namespace
{

//  MD5, encoding, HTTP helpers

static std::string md5_hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);
    const char hex_digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        unsigned char c = digest[i];
        out.push_back(hex_digits[c >> 4]);
        out.push_back(hex_digits[c & 0x0F]);
    }

    return out;
}

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

static bool icontains(const std::string& haystack, const char* needle)
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

static bool http_request_to_string(const char* method, const char* url, pfc::string8& out_body, std::string& out_error)
{
    try
    {
        auto api = standard_api_create_t<http_client>();
        http_request::ptr req = api->create_request(method);
        if (url == nullptr || *url == '\0')
        {
            out_error = "Invalid URL (empty).";
            return false;
        }

        LFM_DEBUG("HTTP " << method << " " << url);

        file::ptr stream = req->run(url, fb2k::noAbort);
        if (!stream.is_valid())
        {
            out_error = "No response stream.";
            return false;
        }

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
        LFM_DEBUG("HTTP request exception: " << (what ? what : "(null)"));
        out_error = what ? what : "HTTP exception";
        return false;
    }
}

//  XML / JSON parsing helpers

static bool response_has_error(const char* body)
{
    if (!body || !*body)
        return true;

    std::string s(body);
    return s.find("\"error\"") != std::string::npos;
}

// Minimal parser to extract Last.fm error code from JSON body.
// Returns 0 if not found / not parseable.
static int lastfm_parse_error_code(const char* body)
{
    if (!body || !*body)
        return 0;

    std::string s(body);

    const char* patterns[] = {"\"error\":", "\"error\" :"};

    for (const char* p : patterns)
    {
        size_t pos = s.find(p);
        if (pos == std::string::npos)
            continue;

        pos = s.find_first_of("0123456789", pos);
        if (pos == std::string::npos)
            continue;

        int code = 0;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos]))
        {
            code = code * 10 + (s[pos] - '0');
            ++pos;
        }

        if (code > 0)
            return code;
    }

    return 0;
}

} // anonymous namespace

//  Public helpers

//  Public low-level scrobble
lastfm_scrobble_result lastfm_scrobble_track(const lastfm_track_info& track, double playback_seconds,
                                             std::time_t start_timestamp)
{
    // Check auth state
    lastfm_auth_state auth_state = lastfm_get_auth_state();
    if (!auth_state.is_authenticated || auth_state.session_key.empty())
    {
        LFM_INFO("lastfm_scrobble_track(): no valid auth state.");
        return lastfm_scrobble_result::invalid_session;
    }

    const std::string api_key = __s66_x3();
    const std::string api_secret = __s64_x9();

    if (api_key.empty() || api_secret.empty())
    {
        LFM_INFO("lastfm_scrobble_track(): API key/secret not configured.");
        return lastfm_scrobble_result::other_error;
    }

    // Compute timestamp: track start time (UTC)
    std::time_t start_ts = 0;

    if (start_timestamp > 0)
    {
        start_ts = start_timestamp;
    }
    else
    {
        std::time_t now = std::time(nullptr);
        if (now <= 0)
            now = 0;
        start_ts = now - static_cast<std::time_t>(playback_seconds);
    }

    // Build raw params for signature (no encoding here).
    std::map<std::string, std::string> params = {
        {"api_key", api_key},         {"artist", track.artist},
        {"track", track.title},       {"timestamp", std::to_string(static_cast<long long>(start_ts))},
        {"method", "track.scrobble"}, {"sk", auth_state.session_key},
    };

    if (!track.album.empty())
        params["album"] = track.album;
    if (track.duration_seconds > 0.0)
        params["duration"] = std::to_string(static_cast<int>(track.duration_seconds));

    // Calculate API signature.
    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += api_secret;
    sig = md5_hex(sig);

    // Build URL with URL-encoded VALUES
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encoded_value = url_encode(kv.second);
        url << kv.first.c_str() << "=" << encoded_value.c_str();
    }

    url << "&api_sig=" << sig.c_str() << "&format=json";

    LFM_DEBUG("Scrobble URL: " << url.c_str());

    // HTTP POST
    pfc::string8 body;
    std::string http_error;

    if (!http_request_to_string("POST", url.c_str(), body, http_error))
    {
        if (!http_error.empty() && (icontains(http_error, "403") || icontains(http_error, "forbidden")))
        {
            LFM_INFO("Scrobble: HTTP 403 / Forbidden (" << http_error.c_str() << "). Treating as invalid session.");
            return lastfm_scrobble_result::invalid_session;
        }

        LFM_INFO("Scrobble: HTTP request failed: " << (http_error.empty() ? "unknown error" : http_error.c_str()));
        return lastfm_scrobble_result::temporary_error;
    }

    const char* body_c = body.c_str();
    LFM_DEBUG("Scrobble response: " << (body_c ? body_c : "(null)"));

    // Parse JSON for error
    if (!response_has_error(body_c))
    {
        LFM_DEBUG("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
        return lastfm_scrobble_result::success;
    }

    int err_code = lastfm_parse_error_code(body_c);

    if (err_code == 9)
    {
        LFM_INFO("Scrobble failed: Invalid session key (API error 9).");
        return lastfm_scrobble_result::invalid_session;
    }

    // Map known retryable API errors (11, 16) to temporary_error so they get queued.
    if (err_code == 11 || err_code == 16)
    {
        LFM_INFO("Scrobble failed: Temporary Last.fm API error (" << err_code
                                                                  << "). Will be cached for retry if applicable.");
        return lastfm_scrobble_result::temporary_error;
    }

    LFM_INFO("Scrobble failed: API error.");
    return lastfm_scrobble_result::other_error;
}

//  Public async submission for ONE track
void lastfm_submit_scrobble_async(const lastfm_track_info& track, double playback_seconds)
{
    lastfm_track_info t_copy = track;
    double played_copy = playback_seconds;

    std::thread(
        [t_copy, played_copy]()
        {
            lastfm_scrobble_result res = lastfm_scrobble_track(t_copy, played_copy, 0);

            switch (res)
            {
            case lastfm_scrobble_result::success:
                // already logged
                break;

            case lastfm_scrobble_result::temporary_error:
            {
                LFM_INFO("Scrobble failed: Temporary error (network/server/API). Queuing for retry.");

                // We don't have a stored wallclock start here, so approximate
                // from "now - played_copy" as a fallback.
                std::time_t now = std::time(nullptr);
                if (now <= 0)
                    now = 0;
                std::time_t start_ts = now - static_cast<std::time_t>(played_copy);

                lastfm_queue::instance().queue_scrobble_for_retry(t_copy, played_copy,
                                                                  /*refresh_on_submit=*/false, start_ts);
                break;
            }

            case lastfm_scrobble_result::invalid_session:
                LFM_INFO("Scrobble result: Invalid session, clearing auth on main thread.");
                fb2k::inMainThread(
                    []()
                    {
                        lastfm_clear_authentication();
                        popup_message::g_show("Your Last.fm session is no longer valid.\n"
                                              "Please authenticate again from the Last.fm menu.",
                                              "Last.fm Scrobbler");
                    });
                break;

            case lastfm_scrobble_result::other_error:
                LFM_INFO("Scrobble failed: API error. Not queued, auth preserved.");
                break;
            }
        })
        .detach();
}
