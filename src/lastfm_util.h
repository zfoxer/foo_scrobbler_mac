//
//  lastfm_util.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

#include <foobar2000/SDK/foobar2000.h>

namespace lastfm
{
namespace util
{
std::string md5HexLower(const std::string& data);
std::string urlEncode(const std::string& value);

bool httpRequestToString(const char* method, const char* url, pfc::string8& outBody, std::string& outError);
bool httpGetToString(const char* url, pfc::string8& outBody, std::string& outError);
bool httpPostToString(const char* url, pfc::string8& outBody, std::string& outError);

// Minimal JSON helpers (not a full parser)
bool jsonFindStringValue(const char* json, const char* key, std::string& out);
bool jsonFindIntValue(const char* json, const char* key, int& out);
bool jsonHasKey(const char* json, const char* key);

} // namespace util
} // namespace lastfm
