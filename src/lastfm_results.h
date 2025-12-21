//
//  lastfm_results.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

enum class LastfmScrobbleResult
{
    SUCCESS,
    TEMPORARY_ERROR,
    INVALID_SESSION,
    OTHER_ERROR
};
