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

// JSON helpers

static const char* skipWs(const char* p)
{
    while (p && *p && std::isspace((unsigned char)*p))
        ++p;
    return p;
}

static bool readJsonString(const char*& p, std::string& out)
{
    out.clear();
    p = skipWs(p);
    if (!p || *p != '"')
        return false;

    ++p; // opening quote

    while (*p)
    {
        char c = *p++;
        if (c == '"')
            return true;

        if (c == '\\')
        {
            char esc = *p++;
            if (!esc)
                return false;

            switch (esc)
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
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
                // Minimal: preserve \uXXXX sequence without decoding.
                out.append("\\u");
                for (int i = 0; i < 4 && *p; ++i)
                    out.push_back(*p++);
                break;
            default:
                out.push_back(esc);
                break;
            }
        }
        else
        {
            out.push_back(c);
        }
    }

    return false;
}

static bool readJsonInt(const char*& p, int& out)
{
    p = skipWs(p);
    if (!p || !*p)
        return false;

    bool neg = false;
    if (*p == '-')
    {
        neg = true;
        ++p;
    }

    if (!std::isdigit((unsigned char)*p))
        return false;

    long long v = 0;
    while (std::isdigit((unsigned char)*p))
    {
        v = v * 10 + (*p - '0');
        ++p;
        if (v > 2147483647LL)
            break;
    }

    out = (int)(neg ? -v : v);
    return true;
}

bool jsonFindStringValue(const char* json, const char* key, std::string& out)
{
    out.clear();
    if (!json || !*json || !key || !*key)
        return false;

    const char* p = json;
    std::string k;

    while (*p)
    {
        p = skipWs(p);
        if (!*p)
            break;

        if (*p != '"')
        {
            ++p;
            continue;
        }

        const char* before = p;
        if (!readJsonString(p, k))
        {
            p = before + 1;
            continue;
        }

        const char* afterKey = skipWs(p);
        if (!afterKey || *afterKey != ':')
            continue;

        p = afterKey + 1;

        if (k != key)
            continue;

        std::string v;
        const char* valuePos = p;
        if (readJsonString(valuePos, v))
        {
            out = std::move(v);
            return true;
        }

        return false; // key matched, value not a string
    }

    return false;
}

bool jsonFindIntValue(const char* json, const char* key, int& out)
{
    out = 0;
    if (!json || !*json || !key || !*key)
        return false;

    const char* p = json;
    std::string k;

    while (*p)
    {
        p = skipWs(p);
        if (!*p)
            break;

        if (*p != '"')
        {
            ++p;
            continue;
        }

        const char* before = p;
        if (!readJsonString(p, k))
        {
            p = before + 1;
            continue;
        }

        const char* afterKey = skipWs(p);
        if (!afterKey || *afterKey != ':')
            continue;

        p = afterKey + 1;

        if (k != key)
            continue;

        const char* valuePos = p;
        int v = 0;
        if (readJsonInt(valuePos, v))
        {
            out = v;
            return true;
        }

        return false; // key matched, value not an int
    }

    return false;
}

bool jsonHasKey(const char* json, const char* key)
{
    if (!json || !key || !*key)
        return false;

    std::string s;
    if (jsonFindStringValue(json, key, s))
        return true;

    int i = 0;
    if (jsonFindIntValue(json, key, i))
        return true;

    // Fallback for bool/null/object/array: find "key" followed by :
    const std::string needle = std::string("\"") + key + "\"";
    const char* p = std::strstr(json, needle.c_str());
    while (p)
    {
        const char* after = skipWs(p + needle.size());
        if (after && *after == ':')
            return true;

        p = std::strstr(p + 1, needle.c_str());
    }

    return false;
}

LastfmApiErrorInfo extractLastfmApiError(const char* body)
{
    LastfmApiErrorInfo info;

    if (!body)
        return info;

    const char* p = body;
    while (*p && std::isspace((unsigned char)*p))
        ++p;

    if (*p != '{' || std::strchr(p, '}') == nullptr)
        return info;

    info.hasJson = true;

    int errCode = 0;
    if (jsonFindIntValue(body, "error", errCode))
    {
        info.hasError = true;
        info.errorCode = errCode;
        jsonFindStringValue(body, "message", info.message);
    }

    return info;
}

} // namespace util
} // namespace lastfm
