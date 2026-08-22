//
//  lastfm_util.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>
#include <vector>

#include <foobar2000/SDK/foobar2000.h>

namespace lastfm
{
namespace util
{
struct LastfmApiErrorInfo
{
    bool hasJson = false;
    bool hasError = false;
    int errorCode = 0;
    std::string message;

    // Present on a track.scrobble reply
    bool hasScrobbleCounts = false;
    int acceptedCount = 0;
    int ignoredCount = 0;
};
LastfmApiErrorInfo extractLastfmApiError(const char* body);

std::string cleanTagValue(const char* value);
std::string md5HexLower(const std::string& data);
bool fooScrobblerTagAllowsSubmission(const file_info& info);
bool isVariousArtistsValue(const std::string& value);
bool isNetworkStreamPath(const char* path);
bool looksLikeStationTitle(const std::string& title);
bool parseArtistTitleFromCombined(const std::string& combined, std::string& artist, std::string& title);
bool extractStreamArtistTitle(const file_info& info, std::string& outArtist, std::string& outTitle,
                              std::string& outAlbum);
std::string urlEncode(const std::string& value);

bool httpRequestToString(const char* method, const char* url, pfc::string8& outBody, std::string& outError,
                         abort_callback& abort = fb2k::noAbort);
bool httpGetToString(const char* url, pfc::string8& outBody, std::string& outError,
                     abort_callback& abort = fb2k::noAbort);
bool httpPostToString(const char* url, pfc::string8& outBody, std::string& outError,
                      abort_callback& abort = fb2k::noAbort);
bool httpPostFormToString(const char* url, const std::string& formBody, pfc::string8& outBody, std::string& outError,
                          abort_callback& abort = fb2k::noAbort);

// Small strict JSON parser for Last.fm responses: full grammar, no array indexing.
namespace json
{
struct Value
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;              // String payload
    std::vector<std::string> keys; // Object member names, parallel to items
    std::vector<Value> items;      // Array elements, or object member values

    bool isObject() const
    {
        return type == Type::Object;
    }

    // Dotted-path lookup from this node, e.g., at("session.key").
    const Value* at(const char* path) const;

    // Typed reads.
    bool asInt(int& out) const;
    bool asString(std::string& out) const;
};

// Parses one whole document. Trailing garbage is rejected.
bool parse(const char* text, Value& out);

// Parse and read the string at a dotted path in one call.
bool findString(const char* text, const char* path, std::string& out);
} // namespace json

} // namespace util
} // namespace lastfm
