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
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <CommonCrypto/CommonDigest.h>

#include <map>
#include <ctime>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

namespace
{

//  Persistent queue storage
static const GUID guid_cfg_lastfm_pending_scrobbles = {
    0x9b3b2c41, 0x4c2d, 0x4d8f, {0x9a, 0xa1, 0x1e, 0x37, 0x5b, 0x6a, 0x82, 0x19}};

static cfg_string cfg_lastfm_pending_scrobbles(guid_cfg_lastfm_pending_scrobbles, "");

// Very simple line-based format:
// artist \t title \t album \t duration_seconds \t playback_seconds
struct queued_scrobble
{
    std::string artist;
    std::string title;
    std::string album;
    double duration_seconds = 0.0;
    double playback_seconds = 0.0;
};

static std::string serialize_scrobble(const queued_scrobble& q)
{
    std::string out;
    out.reserve(128);
    out += q.artist;
    out += '\t';
    out += q.title;
    out += '\t';
    out += q.album;
    out += '\t';
    out += std::to_string(q.duration_seconds);
    out += '\t';
    out += std::to_string(q.playback_seconds);
    return out;
}

static bool parse_double(const char* s, double& out)
{
    if (!s || !*s)
        return false;
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s)
        return false;
    out = v;
    return true;
}

static std::vector<queued_scrobble> load_pending_scrobbles()
{
    std::vector<queued_scrobble> result;

    pfc::string8 raw = cfg_lastfm_pending_scrobbles.get();
    const char* data = raw.c_str();
    if (!data || !*data)
        return result;

    const char* line_start = data;
    while (*line_start)
    {
        const char* line_end = std::strchr(line_start, '\n');
        std::string line;
        if (line_end)
        {
            line.assign(line_start, line_end - line_start);
            line_start = line_end + 1;
        }
        else
        {
            line.assign(line_start);
            line_start += line.size();
        }

        if (line.empty())
            continue;

        std::vector<std::string> parts;
        size_t pos = 0;
        while (true)
        {
            size_t tab = line.find('\t', pos);
            if (tab == std::string::npos)
            {
                parts.push_back(line.substr(pos));
                break;
            }
            parts.push_back(line.substr(pos, tab - pos));
            pos = tab + 1;
        }

        if (parts.size() < 5)
            continue;

        queued_scrobble q;
        q.artist = parts[0];
        q.title = parts[1];
        q.album = parts[2];

        double dur = 0.0, pb = 0.0;
        if (!parse_double(parts[3].c_str(), dur))
            dur = 0.0;
        if (!parse_double(parts[4].c_str(), pb))
            pb = 0.0;
        q.duration_seconds = dur;
        q.playback_seconds = pb;

        result.push_back(q);
    }

    return result;
}

static void save_pending_scrobbles(const std::vector<queued_scrobble>& items)
{
    pfc::string8 raw;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            raw << "\n";
        std::string line = serialize_scrobble(items[i]);
        raw << line.c_str();
    }
    cfg_lastfm_pending_scrobbles.set(raw);
}

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
        const unsigned char b = digest[i];
        out.push_back(hex_digits[b >> 4]);
        out.push_back(hex_digits[b & 0x0F]);
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
            out.push_back((char)c);
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

// Generic helper: run HTTP request with explicit method ("POST")
// and read entire body into pfc::string8.
// On error, returns false and fills out_error with the textual reason.
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

// Helper: cheap, case-insensitive substring check on std::string
static bool icontains(const std::string& haystack, const char* needle)
{
    if (needle == nullptr || *needle == '\0')
        return false;

    std::string lower_h = haystack;
    std::string lower_n = needle;

    for (char& c : lower_h)
        c = (char)std::tolower((unsigned char)c);
    for (char& c : lower_n)
        c = (char)std::tolower((unsigned char)c);

    return lower_h.find(lower_n) != std::string::npos;
}

// Process all queued scrobbles synchronously.
// Used by lastfm_retry_queued_scrobbles() and from async wrapper.
static void process_scrobble_queue()
{
    std::vector<queued_scrobble> items = load_pending_scrobbles();
    if (items.empty())
        return;

    if ((unsigned)items.size() > 1)
        LFM_INFO("Submitting " << (unsigned)items.size() << " queued scrobble(s).");

    std::vector<queued_scrobble> remaining;
    bool invalid_session_seen = false;

    for (const auto& q : items)
    {
        if (invalid_session_seen)
        {
            remaining.push_back(q);
            continue;
        }

        lastfm_auth_state auth_state = lastfm_get_auth_state();
        if (!auth_state.is_authenticated || auth_state.session_key.empty())
        {
            LFM_INFO("Queue retry: no valid auth state, keeping remaining items.");
            remaining.push_back(q);
            invalid_session_seen = true;
            continue;
        }

        lastfm_track_info t;
        t.artist = q.artist;
        t.title = q.title;
        t.album = q.album;
        t.mbid.clear();
        t.duration_seconds = q.duration_seconds;

        lastfm_scrobble_result res = lastfm_scrobble_track(t, q.playback_seconds);

        switch (res)
        {
        case lastfm_scrobble_result::success:
        {
            LFM_DEBUG("Queued scrobble succeeded: " << t.artist.c_str() << " - " << t.title.c_str());
            break;
        }

        case lastfm_scrobble_result::temporary_error:
        case lastfm_scrobble_result::other_error:
            remaining.push_back(q);
            break;

        case lastfm_scrobble_result::invalid_session:
        {
            remaining.push_back(q);
            invalid_session_seen = true;

            LFM_INFO("Queue retry: invalid session detected. Clearing auth on main thread.");

            fb2k::inMainThread(
                []()
                {
                    clear_lastfm_authentication();
                    popup_message::g_show("Your Last.fm session is no longer valid.\n"
                                          "Please authenticate again from the Last.fm menu.",
                                          "Last.fm Scrobbler");
                });
            break;
        }
        }
    }

    save_pending_scrobbles(remaining);

    if ((unsigned)remaining.size())
        LFM_INFO("Pending scrobbles remaining: " << (unsigned)remaining.size());
}

} // anonymous namespace

//  Public queue helpers
void lastfm_queue_scrobble_for_retry(const lastfm_track_info& track, double playback_seconds)
{
    if (track.title.empty() || track.artist.empty())
    {
        LFM_INFO("Missing track info, not submitting.");
        return;
    }

    queued_scrobble q;
    q.artist = track.artist;
    q.title = track.title;
    q.album = track.album;
    q.duration_seconds = track.duration_seconds;
    q.playback_seconds = playback_seconds;

    std::vector<queued_scrobble> items = load_pending_scrobbles();
    items.push_back(q);
    save_pending_scrobbles(items);

    LFM_DEBUG("Queued scrobble for retry: " << track.artist.c_str() << " - " << track.title.c_str());
}

void lastfm_retry_queued_scrobbles()
{
    process_scrobble_queue();
}

void lastfm_retry_queued_scrobbles_async()
{
    std::thread([]() { process_scrobble_queue(); }).detach();
}

//  Public low-level scrobble
lastfm_scrobble_result lastfm_scrobble_track(const lastfm_track_info& track, double playback_seconds)
{
    // 1) Check auth state
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

    // 2) Compute timestamp: track start time (UTC)
    std::time_t now = std::time(nullptr);
    if (now <= 0)
        now = 0;

    const std::time_t start_ts = now - static_cast<std::time_t>(playback_seconds);

    // 3) Build raw params for signature (no encoding here).
    std::map<std::string, std::string> params = {
        {"api_key", api_key},         {"artist", track.artist},
        {"track", track.title},       {"timestamp", std::to_string(static_cast<long long>(start_ts))},
        {"method", "track.scrobble"}, {"sk", auth_state.session_key},
    };

    if (!track.album.empty())
        params["album"] = track.album;
    if (track.duration_seconds > 0.0)
        params["duration"] = std::to_string(static_cast<int>(track.duration_seconds));

    // 4) Calculate API signature.
    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += api_secret;
    sig = md5_hex(sig);

    // 5) Build URL with URL-encoded VALUES
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

    LFM_DEBUG("Scrobble URL: " << url;);

    // 6) Fire the request as POST
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

    // 7) Inspect response
    if (!response_has_error(body_c))
    {
        LFM_DEBUG("Scrobble OK.");
        return lastfm_scrobble_result::success;
    }

    if (response_is_invalid_session(body_c))
    {
        LFM_INFO("Scrobble failed: Invalid session key (API error 9).");
        return lastfm_scrobble_result::invalid_session;
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
            lastfm_scrobble_result res = lastfm_scrobble_track(t_copy, played_copy);

            switch (res)
            {
            case lastfm_scrobble_result::success:
                // already logged
                break;

            case lastfm_scrobble_result::temporary_error:
                LFM_INFO("Scrobble failed: Temporary error (network/server). Queuing for retry.");
                lastfm_queue_scrobble_for_retry(t_copy, played_copy);
                break;

            case lastfm_scrobble_result::invalid_session:
                LFM_INFO("Scrobble result: Invalid session, clearing auth on main thread.");
                fb2k::inMainThread(
                    []()
                    {
                        clear_lastfm_authentication();
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
