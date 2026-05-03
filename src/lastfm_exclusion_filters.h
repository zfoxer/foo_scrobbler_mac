//
//  lastfm_exclusion_filters.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

namespace lastfm
{
namespace exclusion_filters
{

bool isExcludedByTextOrRegexFilters(const std::string& artist, const std::string& title, const std::string& album);

} // namespace exclusion_filters
} // namespace lastfm
