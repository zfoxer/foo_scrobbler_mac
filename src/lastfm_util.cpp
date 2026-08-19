//
//  lastfm_util.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_util.h"
#include "debug.h"

#include <string>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace lastfm
{
namespace util
{
static std::string redact_url_for_log(const char* url)
{
    if (!url)
        return "(null)";

    std::string s(url);

    // If there's a query, don't log it.
    auto q = s.find('?');
    if (q != std::string::npos)
    {
        s.resize(q);
        s += "?<redacted>";
    }
    return s;
}

static bool readHttpStreamToString(file::ptr stream, pfc::string8& outBody, std::string& outError)
{
    if (!stream.is_valid())
    {
        outError = "No response stream.";
        return false;
    }

    pfc::string8 line;
    while (!stream->is_eof(fb2k::noAbort))
    {
        line.reset();
        stream->read_string_raw(line, fb2k::noAbort);
        outBody += line;
    }

    return true;
}

std::string cleanTagValue(const char* value)
{
    if (!value)
        return {};

    std::string s(value);

    std::size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;

    std::size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1]))
        --end;

    if (start == end)
        return {};

    s = s.substr(start, end - start);

    std::string norm;
    for (char c : s)
        if (!std::isspace((unsigned char)c))
            norm.push_back((char)std::tolower((unsigned char)c));

    if (norm == "unknown" || norm == "unknownartist" || norm == "unknowntrack")
        return {};

    return s;
}

static std::optional<bool> parseFooScrobblerTagValue(const char* value)
{
    const std::string v = cleanTagValue(value);
    if (v.empty())
        return std::nullopt;

    std::string lower;
    lower.reserve(v.size());
    for (unsigned char c : v)
        lower.push_back((char)std::tolower(c));

    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
        return true;

    if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
        return false;

    return std::nullopt;
}

bool fooScrobblerTagAllowsSubmission(const file_info& info)
{
    const t_size index = info.meta_find("FOO_SCROBBLER");
    if (index == SIZE_MAX)
        return true;

    const t_size valueCount = info.meta_enum_value_count(index);
    for (t_size i = 0; i < valueCount; ++i)
    {
        std::optional<bool> parsed = parseFooScrobblerTagValue(info.meta_enum_value(index, i));
        if (parsed.has_value())
            return *parsed;
    }

    return true;
}

bool isVariousArtistsValue(const std::string& value)
{
    std::string s;
    bool lastWasSpace = false;

    const char* p = value.c_str();
    std::size_t remaining = value.size();

    while (remaining > 0)
    {
        unsigned c = 0;
        const std::size_t used = pfc::utf8_decode_char(p, c, remaining);
        if (used == 0)
            return false;

        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            s.push_back((char)c);
            lastWasSpace = false;
        }
        else if (c == '.' || c == '/')
        {
        }
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x00A0 || c == 0x3000 ||
                 (c >= 0x2000 && c <= 0x200A))
        {
            if (!s.empty() && !lastWasSpace)
            {
                s.push_back(' ');
                lastWasSpace = true;
            }
        }
        else
            return false;

        p += used;
        remaining -= used;
    }

    if (!s.empty() && s.back() == ' ')
        s.pop_back();

    return s == "various artists" || s == "various artist" || s == "variousartists" || s == "various" || s == "va" ||
           s == "v a";
}

bool isNetworkStreamPath(const char* path)
{
    if (!path)
        return false;

    // Be strict: foobar can use pseudo-schemes like foo:// for local container tracks (ISO, etc).
    return (std::strncmp(path, "http://", 7) == 0) || (std::strncmp(path, "https://", 8) == 0) ||
           (std::strncmp(path, "mms://", 6) == 0) || (std::strncmp(path, "rtsp://", 7) == 0) ||
           (std::strncmp(path, "icy://", 6) == 0);
}

// A single CJK ideograph / kana / hangul syllable is information-dense and is a
// legitimate (often whole) track title, unlike a lone Latin letter or a symbol.
static bool isCjkLetter(unsigned c)
{
    return (c >= 0x3040 && c <= 0x30FF) || // Hiragana + Katakana
           (c >= 0x3400 && c <= 0x9FFF) || // CJK Ext-A + Unified Ideographs
           (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility Ideographs
           (c >= 0xAC00 && c <= 0xD7A3) || // Hangul syllables
           (c >= 0xFF66 && c <= 0xFF9D) || // Halfwidth Katakana
           (c >= 0x20000 && c <= 0x2FA1F); // CJK Ext-B+ (supplementary plane)
}

bool looksLikeStationTitle(const std::string& title)
{
    if (title.empty())
        return true;

    // Long sentences, slogans, blurbs, station branding, bs.
    if (title.size() > 80)
        return true;

    int content = 0;
    int spaces = 0;
    int cjkLetters = 0;

    bool hasBracket = false;
    bool hasUrl = false;

    std::string norm;
    norm.reserve(title.size());

    const char* p = title.c_str();
    std::size_t remaining = title.size();
    while (remaining > 0)
    {
        unsigned c = 0;
        const std::size_t used = pfc::utf8_decode_char(p, c, remaining);
        if (used == 0)
            return true;

        if (c >= 'A' && c <= 'Z')
            norm.push_back((char)(c - 'A' + 'a'));
        else if (c < 0x80)
            norm.push_back((char)c);
        else
            norm.push_back(' ');

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x00A0 || c == 0x3000 ||
            (c >= 0x2000 && c <= 0x200A))
            ++spaces;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c > 0x7F)
            ++content;

        if (isCjkLetter(c))
            ++cjkLetters;

        if (c == '[' || c == ']')
            hasBracket = true;

        p += used;
        remaining -= used;
    }

    if (norm.find("http") != std::string::npos || norm.find("www.") != std::string::npos)
        hasUrl = true;

    if (content < 3 && cjkLetters == 0)
        return true;

    if (hasBracket)
        return true;

    if (spaces > (int)title.size() / 3)
        return true;

    if (hasUrl)
        return true;

    return false;
}

bool parseArtistTitleFromCombined(const std::string& combined, std::string& artist, std::string& title)
{
    const char* seps[] = {" - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", ": "};
    for (const char* sep : seps)
    {
        const std::size_t pos = combined.find(sep);
        if (pos == std::string::npos)
            continue;

        const std::string left = combined.substr(0, pos);
        const std::string right = combined.substr(pos + std::strlen(sep));

        artist = cleanTagValue(left.c_str());
        title = cleanTagValue(right.c_str());

        if (artist.empty() || title.empty())
            continue;

        if (looksLikeStationTitle(title))
            continue;

        return true;
    }
    return false;
}

bool extractStreamArtistTitle(const file_info& info, std::string& outArtist, std::string& outTitle,
                              std::string& outAlbum)
{
    outArtist.clear();
    outTitle.clear();
    outAlbum.clear();

    auto get1 = [&](const char* key) -> std::string { return cleanTagValue(info.meta_get(key, 0)); };

    auto firstOf = [&](const char* const* keys, std::size_t n) -> std::string
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            std::string v = get1(keys[i]);
            if (!v.empty())
                return v;
        }
        return {};
    };

    // fbar pre-splits stream metadata into separate artist/title tags; trust it.
    const bool haveExplicitArtist = !get1("artist").empty() || !get1("ARTIST").empty();

    static const char* kCombined[] = {"streamtitle", "StreamTitle", "STREAMTITLE", "icy-title",
                                      "Icy-Title",   "ICY-TITLE",   "title",       "TITLE"};

    if (!haveExplicitArtist)
    {
        std::string combined = firstOf(kCombined, sizeof(kCombined) / sizeof(kCombined[0]));
        if (!combined.empty())
        {
            std::string a, t;
            if (parseArtistTitleFromCombined(combined, a, t))
            {
                outArtist = a;
                outTitle = t;
                return true;
            }
        }
    }

    // If no combined parse, try explicit.
    static const char* kArtist[] = {"artist", "ARTIST"};
    static const char* kTitle[] = {"title", "TITLE"};
    static const char* kAlbum[] = {"album", "ALBUM"};

    std::string a = firstOf(kArtist, sizeof(kArtist) / sizeof(kArtist[0]));
    std::string t = firstOf(kTitle, sizeof(kTitle) / sizeof(kTitle[0]));
    std::string al = firstOf(kAlbum, sizeof(kAlbum) / sizeof(kAlbum[0]));

    // If title looks like station branding/slogan/bs, reject it.
    if (!t.empty() && looksLikeStationTitle(t))
        t.clear();

    if (!a.empty() && !t.empty())
    {
        outArtist = a;
        outTitle = t;
        outAlbum = al;
        return true;
    }

    return false;
}

std::string md5HexLower(const std::string& data)
{
    const auto digest = hasher_md5::get()->process_single(data.data(), data.size());
    pfc::string8 hex = pfc::format_hexdump_lowercase(digest.m_data, sizeof(digest.m_data), "");

    return std::string(hex.c_str());
}

std::string urlEncode(const std::string& value)
{
    std::string out;
    out.reserve(value.size() * 3);

    static const char* hex = "0123456789ABCDEF";

    for (unsigned char c : value)
    {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~';

        if (unreserved)
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

bool httpRequestToString(const char* method, const char* url, pfc::string8& outBody, std::string& outError)
{
    outBody.reset();
    outError.clear();

    if (!method || !*method)
    {
        outError = "Invalid HTTP method (empty).";
        return false;
    }
    if (!url || !*url)
    {
        outError = "Invalid URL (empty).";
        return false;
    }

    try
    {
        auto client = standard_api_create_t<http_client>();
        http_request::ptr req = client->create_request(method);

        LFM_DEBUG("HTTP " << method << " " << redact_url_for_log(url).c_str());

        file::ptr stream = req->run_ex(url, fb2k::noAbort);
        return readHttpStreamToString(stream, outBody, outError);
    }
    catch (const std::exception& e)
    {
        outError = e.what() ? e.what() : "HTTP exception";
        LFM_DEBUG("HTTP exception: " << (outError.empty() ? "(empty)" : outError.c_str()));
        return false;
    }
}

bool httpGetToString(const char* url, pfc::string8& outBody, std::string& outError)
{
    return httpRequestToString("GET", url, outBody, outError);
}

bool httpPostToString(const char* url, pfc::string8& outBody, std::string& outError)
{
    return httpRequestToString("POST", url, outBody, outError);
}

bool httpPostFormToString(const char* url, const std::string& formBody, pfc::string8& outBody, std::string& outError)
{
    outBody.reset();
    outError.clear();

    if (!url || !*url)
    {
        outError = "Invalid URL (empty).";
        return false;
    }

    try
    {
        auto client = standard_api_create_t<http_client>();
        http_request::ptr req = client->create_request("POST");

        service_ptr_t<http_request_post_v2> post;
        if (!req->service_query_t(post))
        {
            outError = "HTTP POST body interface is unavailable.";
            return false;
        }

        LFM_DEBUG("HTTP POST " << redact_url_for_log(url).c_str() << " bodyBytes=" << formBody.size());

        post->set_post_data(formBody.data(), formBody.size(), "application/x-www-form-urlencoded");
        file::ptr stream = post->run_ex(url, fb2k::noAbort);
        return readHttpStreamToString(stream, outBody, outError);
    }
    catch (const std::exception& e)
    {
        outError = e.what() ? e.what() : "HTTP exception";
        LFM_DEBUG("HTTP POST form exception: " << (outError.empty() ? "(empty)" : outError.c_str()));
        return false;
    }
}

namespace json
{
namespace
{
constexpr int kMaxDepth = 32;

void skipWs(const char*& p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        ++p;
}

void appendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80)
        out.push_back((char)cp);
    else if (cp < 0x800)
    {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

bool readHex4(const char*& p, unsigned& out)
{
    out = 0;
    for (int i = 0; i < 4; ++i, ++p)
    {
        const unsigned char c = (unsigned char)*p;
        if (!std::isxdigit(c))
            return false;

        out = (out << 4) | (unsigned)(std::isdigit(c) ? c - '0' : (std::tolower(c) - 'a' + 10));
    }

    return true;
}

bool parseString(const char*& p, std::string& out)
{
    out.clear();
    if (*p != '"')
        return false;

    ++p;
    while ((unsigned char)*p >= 0x20) // Stops on control chars and on the terminating NUL
    {
        const char c = *p++;
        if (c == '"')
            return true;

        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }

        if ((unsigned char)*p < 0x20)
            return false;

        const char esc = *p++;
        switch (esc)
        {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
        {
            unsigned cp = 0;
            if (!readHex4(p, cp))
                return false;

            if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') // Surrogate pair
            {
                const char* save = p;
                unsigned low = 0;
                p += 2;
                if (readHex4(p, low) && low >= 0xDC00 && low <= 0xDFFF)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                else
                    p = save;
            }

            if (cp >= 0xD800 && cp <= 0xDFFF) // Unpaired surrogate: keep the output valid UTF-8
                cp = 0xFFFD;

            appendUtf8(out, cp);
            break;
        }
        default:
            return false;
        }
    }

    return false;
}

bool parseNumber(const char*& p, double& out)
{
    const char* start = p;
    auto digits = [&p]
    {
        const char* from = p;
        while (std::isdigit((unsigned char)*p))
            ++p;

        return p != from;
    };

    if (*p == '-')
        ++p;
    if (!digits())
        return false;

    if (*p == '.')
    {
        ++p;
        if (!digits())
            return false;
    }

    if (*p == 'e' || *p == 'E')
    {
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        if (!digits())
            return false;
    }

    out = std::strtod(std::string(start, p).c_str(), nullptr);
    return true;
}

bool matchLiteral(const char*& p, const char* literal)
{
    const std::size_t n = std::strlen(literal);
    if (std::strncmp(p, literal, n) != 0)
        return false;

    p += n;
    return true;
}

bool parseValue(const char*& p, Value& out, int depth)
{
    if (depth > kMaxDepth)
        return false;

    skipWs(p);
    switch (*p)
    {
    case '"':
        out.type = Value::Type::String;
        return parseString(p, out.text);

    case 't':
        out.type = Value::Type::Bool;
        out.boolean = true;
        return matchLiteral(p, "true");

    case 'f':
        out.type = Value::Type::Bool;
        return matchLiteral(p, "false");

    case 'n':
        return matchLiteral(p, "null");

    case '{':
    case '[':
    {
        const bool object = (*p == '{');
        const char close = object ? '}' : ']';
        out.type = object ? Value::Type::Object : Value::Type::Array;

        ++p;
        skipWs(p);
        if (*p == close)
        {
            ++p;
            return true;
        }

        for (;;)
        {
            if (object)
            {
                std::string key;
                skipWs(p);
                if (!parseString(p, key))
                    return false;

                skipWs(p);
                if (*p != ':')
                    return false;

                ++p;
                out.keys.push_back(std::move(key));
            }

            out.items.emplace_back();
            if (!parseValue(p, out.items.back(), depth + 1))
                return false;

            skipWs(p);
            if (*p == ',')
            {
                ++p;
                continue;
            }

            if (*p != close)
                return false;

            ++p;
            return true;
        }
    }

    default:
        out.type = Value::Type::Number;
        return parseNumber(p, out.number);
    }
}
} // namespace

const Value* Value::at(const char* path) const
{
    const Value* node = this;
    while (path && *path)
    {
        if (!node->isObject())
            return nullptr;

        const char* dot = std::strchr(path, '.');
        const std::size_t n = dot ? (std::size_t)(dot - path) : std::strlen(path);
        const Value* next = nullptr;

        for (std::size_t i = 0; i < node->keys.size() && i < node->items.size(); ++i)
        {
            if (node->keys[i].compare(0, std::string::npos, path, n) == 0)
            {
                next = &node->items[i];
                break;
            }
        }

        if (!next)
            return nullptr;

        node = next;
        path += dot ? n + 1 : n;
    }

    return node;
}

bool Value::asInt(int& out) const
{
    double v = number;

    if (type == Type::String)
    {
        // Last.fm gives some integers as strings.
        const char* p = text.c_str();
        if (!parseNumber(p, v) || *p != '\0')
            return false;
    }
    else if (type != Type::Number)
        return false;

    if (v < -2147483648.0 || v > 2147483647.0)
        return false;

    out = (int)v;
    return true;
}

bool Value::asString(std::string& out) const
{
    if (type != Type::String)
        return false;

    out = text;
    return true;
}

bool parse(const char* text, Value& out)
{
    out = Value();
    if (!text)
        return false;

    const char* p = text;
    if (!parseValue(p, out, 0))
        return false;

    skipWs(p);
    return *p == '\0';
}

bool findString(const char* text, const char* path, std::string& out)
{
    Value root;
    const Value* node = parse(text, root) ? root.at(path) : nullptr;
    return node && node->asString(out);
}
} // namespace json

LastfmApiErrorInfo extractLastfmApiError(const char* body)
{
    LastfmApiErrorInfo info;

    json::Value root;
    if (!json::parse(body, root) || !root.isObject())
        return info;

    info.hasJson = true;

    const json::Value* error = root.at("error");
    if (error && error->asInt(info.errorCode))
    {
        info.hasError = true;
        if (const json::Value* message = root.at("message"))
            message->asString(info.message);
    }

    return info;
}

} // namespace util
} // namespace lastfm
