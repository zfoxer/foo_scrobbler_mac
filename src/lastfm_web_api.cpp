//
//  lastfm_web_api.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos.
//

#include "lastfm_web_api.h"
#include "lastfm_no.h"
#include "lastfm_state.h"
#include "lastfm_util.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cassert>

namespace
{
using ApiParams = std::map<std::string, std::string>;
using JsonValue = lastfm::util::json::Value;

struct ApiOutcome
{
    LastfmScrobbleResult result = LastfmScrobbleResult::OTHER_ERROR;
    int apiError = 0;
    std::string apiMessage;
    bool hasJson = false;
    int ignoredCount = 0;
};

struct ScrobbleAuth
{
    LastfmAuthState state;
    std::string apiKey;
    std::string apiSecret;
};

static const char* scrobbleResultToString(LastfmScrobbleResult result)
{
    switch (result)
    {
    case LastfmScrobbleResult::SUCCESS:
        return "SUCCESS";
    case LastfmScrobbleResult::TEMPORARY_ERROR:
        return "TEMPORARY_ERROR";
    case LastfmScrobbleResult::RATE_LIMITED:
        return "RATE_LIMITED";
    case LastfmScrobbleResult::INVALID_SESSION:
        return "INVALID_SESSION";
    case LastfmScrobbleResult::OTHER_ERROR:
        return "OTHER_ERROR";
    }

    return "UNKNOWN";
}

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

        case 8:
        case 11:
        case 16:
            out.result = LastfmScrobbleResult::TEMPORARY_ERROR;
            break;
        case 29:
            out.result = LastfmScrobbleResult::RATE_LIMITED;
            break;
        default:
            out.result = LastfmScrobbleResult::OTHER_ERROR;
            break;
        }

        LFM_INFO("Last.fm API error " << apiInfo.errorCode << (apiInfo.message.empty() ? "" : ": ")
                                      << apiInfo.message.c_str());
        return out;
    }

    out.ignoredCount = apiInfo.ignoredCount;

    if (apiInfo.hasScrobbleCounts && apiInfo.ignoredCount > 0)
    {
        LFM_INFO("Last.fm ignored " << apiInfo.ignoredCount << " of " << (apiInfo.acceptedCount + apiInfo.ignoredCount)
                                    << " scrobbles, still reported as success.");
    }

    // Success
    out.result = LastfmScrobbleResult::SUCCESS;
    return out;
}

static bool extractTrackOutcomes(const char* body, std::vector<LastfmTrackOutcome>& out)
{
    out.clear();

    JsonValue root;
    if (!lastfm::util::json::parse(body, root))
        return false;

    const JsonValue* node = root.at("scrobbles.scrobble");
    if (!node)
        return false;

    const bool single = node->isObject();
    if (!single && node->type != JsonValue::Type::Array)
        return false;

    const std::size_t count = single ? 1 : node->items.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const JsonValue& entry = single ? *node : node->items[i];
        if (!entry.isObject())
            return false;

        LastfmTrackOutcome outcome;
        const JsonValue* code = entry.at("ignoredMessage.code");

        int value = 0;
        if (code && code->asInt(value) && value != 0)
        {
            outcome.accepted = false;
            outcome.ignoredCode = value;

            if (const JsonValue* text = entry.at("ignoredMessage.#text"))
                text->asString(outcome.ignoredText);
        }

        out.push_back(std::move(outcome));
    }

    return true;
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

    {
        // A nested "error" is not a top-level API error.
        auto info = lastfm::util::extractLastfmApiError(
            "{\"scrobbles\":{\"scrobble\":{\"ignoredMessage\":{\"code\":\"1\"},\"error\":7}}}");
        assert(info.hasJson);
        assert(!info.hasError);
        assert(!info.hasScrobbleCounts);
    }

    {
        // Refused tracks are reported next to the accepted ones, not as an API error.
        auto info = lastfm::util::extractLastfmApiError(
            "{\"scrobbles\":{\"@attr\":{\"accepted\":3,\"ignored\":\"2\"},\"scrobble\":[]}}");
        assert(info.hasJson);
        assert(!info.hasError);
        assert(info.hasScrobbleCounts);
        assert(info.acceptedCount == 3);
        assert(info.ignoredCount == 2);
    }

    {
        // JSON glued to something else is not JSON, and neither is a truncated body.
        assert(!lastfm::util::extractLastfmApiError("{\"error\":9} <html>oops</html>").hasJson);
        assert(!lastfm::util::extractLastfmApiError("{\"error\":9").hasJson);
    }

    {
        // \uXXXX is decoded to UTF-8, surrogate pairs included.
        auto info = lastfm::util::extractLastfmApiError("{\"error\":6,\"message\":\"Bj\\u00f6rk \\ud83c\\udfb5\"}");
        assert(info.message == "Bj\xc3\xb6rk \xf0\x9f\x8e\xb5");

        // An unpaired surrogate must leave valid UTF-8: these strings reach NSString.
        auto lone = lastfm::util::extractLastfmApiError("{\"error\":6,\"message\":\"\\ud83cX\"}");
        assert(lone.message == "\xef\xbf\xbdX");
    }

    {
        std::string value;
        const char* session = "{\"session\":{\"name\":\"user\",\"key\":\"sk\"}}";
        assert(lastfm::util::json::findString(session, "session.key", value) && value == "sk");
        assert(!lastfm::util::json::findString(session, "key", value));
    }
}

#endif

static std::string buildApiSignature(const ApiParams& params, const std::string& apiSecret)
{
    std::string sigSrc;
    for (const auto& kv : params)
    {
        sigSrc += kv.first;
        sigSrc += kv.second;
    }
    sigSrc += apiSecret;

    return lastfm::util::md5HexLower(sigSrc);
}

static std::string buildSignedFormBody(const ApiParams& params, const std::string& apiSecret)
{
    const std::string apiSig = buildApiSignature(params, apiSecret);

    std::string body;
    bool first = true;
    auto append = [&](const std::string& key, const std::string& value)
    {
        if (!first)
            body += "&";
        first = false;

        body += lastfm::util::urlEncode(key);
        body += "=";
        body += lastfm::util::urlEncode(value);
    };

    for (const auto& kv : params)
        append(kv.first, kv.second);

    append("api_sig", apiSig);
    append("format", "json");
    return body;
}

static std::time_t resolveStartTimestamp(const LastfmScrobbleRequest& request, std::time_t now)
{
    if (request.startTimestamp > 0)
        return request.startTimestamp;

    if (now <= 0)
        now = 0;

    return now - static_cast<std::time_t>(request.playbackSeconds);
}

static void appendOptionalTrackParams(ApiParams& params, const LastfmTrackInfo& track, const std::string& suffix,
                                      bool roundDuration)
{
    if (!track.album.empty())
        params["album" + suffix] = track.album;
    if (!track.albumArtist.empty())
        params["albumArtist" + suffix] = track.albumArtist;
    if (!track.mbid.empty())
        params["mbid" + suffix] = track.mbid;
    if (track.durationSeconds > 0.0)
    {
        const int duration =
            roundDuration ? static_cast<int>(track.durationSeconds + 0.5) : static_cast<int>(track.durationSeconds);
        params["duration" + suffix] = std::to_string(duration);
    }
}

static bool appendIndexedScrobbleParams(ApiParams& params, const LastfmScrobbleRequest& request, std::size_t index,
                                        std::time_t now)
{
    const LastfmTrackInfo& track = request.track;
    if (track.artist.empty() || track.title.empty())
        return false;

    const std::string suffix = "[" + std::to_string(index) + "]";
    params["artist" + suffix] = track.artist;
    params["track" + suffix] = track.title;
    params["timestamp" + suffix] = std::to_string(static_cast<long long>(resolveStartTimestamp(request, now)));

    appendOptionalTrackParams(params, track, suffix, false);
    return true;
}

static bool buildNowPlayingParams(std::map<std::string, std::string>& params, std::string& apiSecretOut,
                                  const LastfmTrackInfo& track)
{
    LastfmAuthState state = lastfmGetAuthState();
    if (!state.isAuthenticated || state.sessionKey.empty())
    {
        LFM_INFO("NowPlaying: not authenticated, skipping.");
        return false;
    }

    if (track.artist.empty() || track.title.empty())
    {
        LFM_INFO("Missing track info, not submitting.");
        return false;
    }

    const std::string apiKey = __key();
    const std::string apiSecret = __sec();

    if (apiKey.empty() || apiSecret.empty())
    {
        LFM_INFO("NowPlaying: API key/secret not configured.");
        return false;
    }

    apiSecretOut = apiSecret;

    params = {
        {"api_key", apiKey},      {"artist", track.artist},
        {"track", track.title},   {"method", "track.updateNowPlaying"},
        {"sk", state.sessionKey},
    };

    appendOptionalTrackParams(params, track, "", true);
    return true;
}

static LastfmScrobbleResult loadScrobbleAuth(ScrobbleAuth& out, const char* logPrefix)
{
    out.state = lastfmGetAuthState();
    if (!out.state.isAuthenticated || out.state.sessionKey.empty())
    {
        LFM_INFO(logPrefix << ": no valid auth state.");
        return LastfmScrobbleResult::INVALID_SESSION;
    }

    out.apiKey = __key();
    out.apiSecret = __sec();

    if (out.apiKey.empty() || out.apiSecret.empty())
    {
        LFM_INFO(logPrefix << ": API key/secret not configured.");
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    return LastfmScrobbleResult::SUCCESS;
}

static LastfmScrobbleResult postNowPlayingAndClassify(const std::string& formBody, abort_callback& abort)
{
    pfc::string8 body;
    std::string httpError;

    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", formBody, body, httpError, abort);

    if (httpOk)
        LFM_DEBUG("NowPlaying response received. (size=" << body.get_length() << ")");

    const ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_DEBUG("NowPlaying OK.");
        return LastfmScrobbleResult::SUCCESS;
    }

    return outcome.result;
}
} // namespace

LastfmScrobbleResult LastfmWebApi::updateNowPlaying(const LastfmTrackInfo& track, abort_callback& abort)
{
    std::map<std::string, std::string> params;
    std::string apiSecret;

    if (!buildNowPlayingParams(params, apiSecret, track))
    {
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    const std::string formBody = buildSignedFormBody(params, apiSecret);
    return postNowPlayingAndClassify(formBody, abort);
}

LastfmScrobbleResult LastfmWebApi::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp, abort_callback& abort,
                                            LastfmTrackOutcome* outOutcome)
{
    if (outOutcome)
        *outOutcome = LastfmTrackOutcome();

#ifdef LFM_DEBUG
    static bool tested = (selfTest_extractLastfmApiError(), true);
    (void)tested;
#endif

    ScrobbleAuth auth;
    const LastfmScrobbleResult authResult = loadScrobbleAuth(auth, "LastfmWebApi::scrobble()");
    if (authResult != LastfmScrobbleResult::SUCCESS)
        return authResult;

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

    ApiParams params = {
        {"api_key", auth.apiKey},     {"artist", track.artist},
        {"track", track.title},       {"timestamp", std::to_string(static_cast<long long>(startTs))},
        {"method", "track.scrobble"}, {"sk", auth.state.sessionKey},
    };

    appendOptionalTrackParams(params, track, "", false);

    pfc::string8 body;
    std::string httpError;

    const std::string formBody = buildSignedFormBody(params, auth.apiSecret);
    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", formBody, body, httpError, abort);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS && outOutcome && outcome.ignoredCount > 0)
    {
        std::vector<LastfmTrackOutcome> outcomes;
        if (extractTrackOutcomes(body.c_str(), outcomes) && outcomes.size() == 1)
            *outOutcome = outcomes.front();
    }

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
    }

    return outcome.result;
}

LastfmScrobbleResult LastfmWebApi::scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests,
                                                 abort_callback& abort, std::vector<LastfmTrackOutcome>* outPerTrack)
{
    if (outPerTrack)
        outPerTrack->clear();

#ifdef LFM_DEBUG
    static bool tested = (selfTest_extractLastfmApiError(), true);
    (void)tested;
#endif

    if (requests.empty())
        return LastfmScrobbleResult::SUCCESS;

    if (requests.size() > 50)
    {
        LFM_INFO("LastfmWebApi::scrobbleBatch(): batch too large: " << requests.size());
        return LastfmScrobbleResult::OTHER_ERROR;
    }

    ScrobbleAuth auth;
    const LastfmScrobbleResult authResult = loadScrobbleAuth(auth, "LastfmWebApi::scrobbleBatch()");
    if (authResult != LastfmScrobbleResult::SUCCESS)
        return authResult;

    ApiParams params = {
        {"api_key", auth.apiKey},
        {"method", "track.scrobble"},
        {"sk", auth.state.sessionKey},
    };

    const std::time_t now = std::time(nullptr);
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        if (!appendIndexedScrobbleParams(params, requests[i], i, now))
        {
            LFM_INFO("LastfmWebApi::scrobbleBatch(): missing artist/title at index " << (unsigned)i);
            return LastfmScrobbleResult::OTHER_ERROR;
        }
    }

    const std::string bodyText = buildSignedFormBody(params, auth.apiSecret);

    pfc::string8 body;
    std::string httpError;

    const bool httpOk =
        lastfm::util::httpPostFormToString("https://ws.audioscrobbler.com/2.0/", bodyText, body, httpError, abort);

    ApiOutcome outcome = classifyResponse(httpOk, httpError, body);

    if (outcome.result == LastfmScrobbleResult::SUCCESS && outPerTrack && outcome.ignoredCount > 0 &&
        (!extractTrackOutcomes(body.c_str(), *outPerTrack) || outPerTrack->size() != requests.size()))
    {
        LFM_INFO("Scrobble batch: per-track list does not match the batch, discarding it.");
        outPerTrack->clear();
    }

    if (outcome.result == LastfmScrobbleResult::SUCCESS)
    {
        if (outcome.ignoredCount > 0)
        {
            const std::size_t ignored = std::min<std::size_t>(outcome.ignoredCount, requests.size());
            LFM_INFO("Scrobble batch partially accepted: " << (unsigned)(requests.size() - ignored) << " of "
                                                           << (unsigned)requests.size());
        }
        else if (requests.size() == 1)
        {
            const LastfmTrackInfo& track = requests.front().track;
            LFM_INFO("Scrobble OK: " << track.artist.c_str() << " - " << track.title.c_str());
        }
        else
        {
            LFM_INFO("Scrobble batch OK: count=" << (unsigned)requests.size());
        }
    }
    else
        LFM_INFO("Scrobble batch failed: result="
                 << scrobbleResultToString(outcome.result) << " count=" << (unsigned)requests.size()
                 << " apiError=" << outcome.apiError
                 << " apiMessage=" << (outcome.apiMessage.empty() ? "<none>" : outcome.apiMessage.c_str()));

    return outcome.result;
}
