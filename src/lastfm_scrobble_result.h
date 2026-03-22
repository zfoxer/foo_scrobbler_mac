//
//  lastfm_scrobble_result.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos.
//

#pragma once

enum class LastfmScrobbleResult
{
    SUCCESS,
    TEMPORARY_ERROR,
    RATE_LIMITED,
    INVALID_SESSION,
    OTHER_ERROR
};
