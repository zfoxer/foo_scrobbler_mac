//
//  lastfm_queue.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#pragma once

#include <cstddef>
#include <ctime>

struct lastfm_track_info;

// Singleton wrapper around the persistent scrobble queue.
class lastfm_queue
{
  public:
    static lastfm_queue& instance();
    void refresh_pending_scrobble_metadata(const lastfm_track_info& track);
    void queue_scrobble_for_retry(const lastfm_track_info& track, double playback_seconds, bool refresh_on_submit,
                                  std::time_t start_timestamp);
    void retry_queued_scrobbles();
    void retry_queued_scrobbles_async();
    std::size_t get_pending_scrobble_count() const;

  private:
    lastfm_queue() = default;
    lastfm_queue(const lastfm_queue&) = delete;
    lastfm_queue& operator=(const lastfm_queue&) = delete;
    // Internal representation is kept private in the .cpp file.
};
