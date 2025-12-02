//
//  lastfm_scrobble.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <string>
#include <ctime>

struct lastfm_track_info;

enum class lastfm_scrobble_result
{
    success,
    temporary_error,
    invalid_session,
    other_error
};

// Low-level, synchronous scrobble. Does the HTTP + JSON work and returns a result.
// Does NOT queue or clear auth itself.
lastfm_scrobble_result lastfm_scrobble_track(const lastfm_track_info& track, double playback_seconds,
                                             std::time_t start_timestamp = 0);

// Queue a scrobble for later retry (used on network/server failures).
void lastfm_queue_scrobble_for_retry(const lastfm_track_info& track, double playback_seconds);

// Retry any queued scrobbles (synchronous).
void lastfm_retry_queued_scrobbles();

// Async wrappers so the UI thread never blocks on network.

// Submit ONE track asynchronously:
// - If it fails with temporary_error → it is queued.
// - If it fails with invalid_session → auth is cleared on main thread.
void lastfm_submit_scrobble_async(const lastfm_track_info& track, double playback_seconds);

// Retry ALL queued scrobbles asynchronously.
// Called when playback starts/stops.
void lastfm_retry_queued_scrobbles_async();

size_t lastfm_get_pending_scrobble_count();
