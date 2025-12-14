//
//  lastfm_util.cpp
//  foo_scrobbler_mac
//
//  Created by Konstantinos Kyriakopoulos on 14/12/25.
//

#include "lastfm_util.h"
#include "debug.h"

#include <CommonCrypto/CommonDigest.h>

#include <cctype>
#include <cstring>

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

std::string md5HexLower(const std::string& data)
{
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(data.data(), (CC_LONG)data.size(), digest);

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(CC_MD5_DIGEST_LENGTH * 2);

    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; ++i)
    {
        const unsigned char b = digest[i];
        out.push_back(hex[(b >> 4) & 0x0F]);
        out.push_back(hex[b & 0x0F]);
    }

    return out;
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

        file::ptr stream = req->run(url, fb2k::noAbort);
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

// ---- Minimal JSON helpers ----

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

} // namespace util
} // namespace lastfm
