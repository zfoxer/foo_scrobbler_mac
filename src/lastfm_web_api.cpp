//
//  LastfmWebApi.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_web_api.h"

#include "lastfm_no.h"
#include "lastfm_ui.h"
#include "debug.h"
#include "lastfm_nowplaying.h"

#include <foobar2000/SDK/foobar2000.h>

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace
{
static std::string md5Hex(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);

    const char hexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        unsigned char c = digest[i];
        out.push_back(hexDigits[c >> 4]);
        out.push_back(hexDigits[c & 0x0F]);
    }

    return out;
}

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

static bool iContains(const std::string& haystack, const char* needle)
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

static bool httpRequestToString(const char* method, const char* url, pfc::string8& outBody, std::string& outError)
{
    try
    {
        auto api = standard_api_create_t<http_client>();
        http_request::ptr req = api->create_request(method);

        if (url == nullptr || *url == '\0')
        {
            outError = "Invalid URL (empty).";
            return false;
        }

        LFM_DEBUG("HTTP " << method << " " << url);

        file::ptr stream = req->run(url, fb2k::noAbort);
        if (!stream.is_valid())
        {
            outError = "No response stream.";
            return false;
        }

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
        LFM_DEBUG("HTTP request exception: " << (what ? what : "(null)"));
        outError = what ? what : "HTTP exception";
        return false;
    }
}

static bool responseHasError(const char* body)
{
    if (!body || !*body)
        return true;

    std::string s(body);
    return s.find("\"error\"") != std::string::npos;
}

static int parseLastfmErrorCode(const char* body)
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
} // namespace

bool LastfmWebApi::updateNowPlaying(const LastfmTrackInfo& track)
{
    return sendNowPlaying(track.artist, track.title, track.album, track.durationSeconds);
}

LastfmScrobbleResult LastfmWebApi::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp)
{
    // Check auth state
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

    // Compute timestamp: track start time (UTC)
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

    // Build raw params for signature (no encoding here).
    std::map<std::string, std::string> params = {
        {"api_key", apiKey},          {"artist", track.artist},
        {"track", track.title},       {"timestamp", std::to_string(static_cast<long long>(startTs))},
        {"method", "track.scrobble"}, {"sk", authState.sessionKey},
    };

    if (!track.album.empty())
        params["album"] = track.album;
    if (track.durationSeconds > 0.0)
        params["duration"] = std::to_string(static_cast<int>(track.durationSeconds));
    // Calculate API signature.
    std::string sig;
    for (const auto& kv : params)
    {
        sig += kv.first;
        sig += kv.second;
    }
    sig += apiSecret;
    sig = md5Hex(sig);

    // Build URL with URL-encoded VALUES
    pfc::string8 url;
    url << "https://ws.audioscrobbler.com/2.0/?";

    bool first = true;
    for (const auto& kv : params)
    {
        if (!first)
            url << "&";
        first = false;

        const std::string encodedValue = urlEncode(kv.second);
        url << kv.first.c_str() << "=" << encodedValue.c_str();
    }

    url << "&api_sig=" << sig.c_str() << "&format=json";

    LFM_DEBUG("Scrobble URL: " << url.c_str());

    // HTTP POST
    pfc::string8 body;
    std::string httpError;

    if (!httpRequestToString("POST", url.c_str(), body, httpError))
    {
        if (!httpError.empty() && (iContains(httpError, "403") || iContains(httpError, "forbidden")))
        {
            LFM_INFO("Scrobble: HTTP 403 / Forbidden (" << httpError.c_str() << "). Treating as invalid session.");
            return LastfmScrobbleResult::INVALID_SESSION;
        }

        LFM_INFO("Scrobble: HTTP request failed: " << (httpError.empty() ? "unknown error" : httpError.c_str()));
        return LastfmScrobbleResult::TEMPORARY_ERROR;
    }

    const char* bodyC = body.c_str();
    LFM_DEBUG("Scrobble response: " << (bodyC ? bodyC : "(null)"));

    // Parse JSON for error
    if (!responseHasError(bodyC))
    {
        LFM_DEBUG("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
        return LastfmScrobbleResult::SUCCESS;
    }

    int errCode = parseLastfmErrorCode(bodyC);

    if (errCode == 9)
    {
        LFM_INFO("Scrobble failed: Invalid session key (API error 9).");
        return LastfmScrobbleResult::INVALID_SESSION;
    }

    // Map known retryable API errors (11, 16) to temporary_error so they get queued.
    if (errCode == 11 || errCode == 16)
    {
        LFM_INFO("Scrobble failed: Temporary Last.fm API error (" << errCode << ").");
        return LastfmScrobbleResult::TEMPORARY_ERROR;
    }

    LFM_INFO("Scrobble failed: API error.");
    return LastfmScrobbleResult::OTHER_ERROR;
}
