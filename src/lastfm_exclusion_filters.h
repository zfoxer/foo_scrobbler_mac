//
//  lastfm_exclusion_filters.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

#include <string>

#include "lastfm_track_info.h"

namespace lastfm
{
namespace exclusion_filters
{

bool isExcludedByTextOrRegexFilters(const std::string& artist, const std::string& title, const std::string& album);
bool isExcludedByTitleFormattingFilter(const metadb_handle_ptr& track, const LastfmTrackInfo& evaluated,
                                       const file_info* externalInfo = nullptr);

} // namespace exclusion_filters
} // namespace lastfm
