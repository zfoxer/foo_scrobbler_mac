//
//  lastfm_tracker.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_tracker.h"
#include "lastfm_ui.h"
#include "lastfm_auth.h"
#include "lastfm_scrobble.h"
#include "lastfm_nowplaying.h"
#include "debug.h"

#include <thread>
#include <algorithm>

unsigned lastfm_tracker::get_flags()
{
    return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_time | flag_on_playback_seek |
           flag_on_playback_pause;
}

void lastfm_tracker::reset_state()
{
    m_is_playing = false;
    m_scrobble_sent = false;
    m_playback_time = 0.0;

    m_effective_listened_seconds = 0.0;
    m_last_reported_time = 0.0;
    m_have_last_reported_time = false;

    m_rules.reset(0.0);
    m_current = lastfm_track_info();
    m_current_handle.release();
    m_start_wallclock = 0;
}

void lastfm_tracker::update_from_track(const metadb_handle_ptr& track)
{
    m_current_handle = track;

    file_info_impl info;
    if (!track->get_info(info))
    {
        reset_state();
        return;
    }

    const char* artist = info.meta_get("artist", 0);
    const char* title = info.meta_get("title", 0);
    const char* album = info.meta_get("album", 0);
    const char* mbid = info.meta_get("musicbrainz_trackid", 0);

    m_current.artist = artist ? artist : "";
    m_current.title = title ? title : "";
    m_current.album = album ? album : "";
    m_current.mbid = mbid ? mbid : "";
    m_current.duration_seconds = info.get_length();

    m_rules.reset(m_current.duration_seconds);
}

void lastfm_tracker::on_playback_new_track(metadb_handle_ptr track)
{
    reset_state();
    m_is_playing = true;
    m_start_wallclock = std::time(nullptr);

    update_from_track(track);

    LFM_DEBUG("Now playing: " << m_current.artist.c_str() << " - " << m_current.title.c_str());

    if (!lastfm_is_authenticated())
        return;

    // Still flush queued scrobbles even if suspended – they were created earlier.
    lastfm_retry_queued_scrobbles_async();

    // If suspended, do not send new "now playing".
    if (lastfm_is_suspended())
    {
        return;
    }

    const lastfm_track_info info = m_current; // copy
    std::thread([info]() { lastfm_send_now_playing(info.artist, info.title, info.album, info.duration_seconds); })
        .detach();
}

void lastfm_tracker::on_playback_time(double time)
{
    // Update effective listening time based on forward progression only.
    if (m_is_playing && m_current.duration_seconds > 0.0)
    {
        if (!m_have_last_reported_time)
        {
            m_last_reported_time = time;
            m_have_last_reported_time = true;
        }
        else
        {
            const double delta = time - m_last_reported_time;

            // Only count small positive deltas as "listened" time.
            // Large jumps (seeks) or backward moves do not increase effective time.
            if (delta > 0.0 && delta <= 10.0)
            {
                m_effective_listened_seconds += delta;
            }

            m_last_reported_time = time;
        }
    }

    // Keep the original absolute position for other logic (rules etc.).
    m_playback_time = time;
    m_rules.playback_time = time;

    // If the scrobble is already queued (after 50%), poll for tag changes and update queued metadata.
    if (m_scrobble_sent && m_current_handle.is_valid())
    {
        file_info_impl info;
        if (m_current_handle->get_info(info))
        {
            std::string newArtist = info.meta_get("artist", 0) ? info.meta_get("artist", 0) : "";
            std::string newTitle = info.meta_get("title", 0) ? info.meta_get("title", 0) : "";
            std::string newAlbum = info.meta_get("album", 0) ? info.meta_get("album", 0) : "";

            if (newArtist != m_current.artist || newTitle != m_current.title || newAlbum != m_current.album)
            {
                m_current.artist = newArtist;
                m_current.title = newTitle;
                m_current.album = newAlbum;

                lastfm_refresh_pending_scrobble_metadata(m_current);

                // Refresh Now Playing on Last.fm with updated tags
                if (lastfm_is_authenticated() && !lastfm_is_suspended())
                {
                    const lastfm_track_info np = m_current;
                    std::thread([np]() { lastfm_send_now_playing(np.artist, np.title, np.album, np.duration_seconds); })
                        .detach();
                }
            }
        }
    }

    submit_scrobble_if_needed();
}

void lastfm_tracker::on_playback_seek(double time)
{
    if (time < m_current.duration_seconds * 0.5)
        m_rules.mark_skipped();
}

void lastfm_tracker::on_playback_pause(bool paused)
{
    m_rules.paused = paused;
}

void lastfm_tracker::on_playback_stop(play_control::t_stop_reason)
{
    // This may queue the scrobble if not yet queued (edge cases).
    submit_scrobble_if_needed();

    // Now actually submit queued scrobbles.
    lastfm_retry_queued_scrobbles_async();
    reset_state();
}

// Unused but required
void lastfm_tracker::on_playback_starting(play_control::t_track_command, bool)
{
}
void lastfm_tracker::on_playback_edited(metadb_handle_ptr)
{
}
void lastfm_tracker::on_playback_dynamic_info(const file_info&)
{
}
void lastfm_tracker::on_playback_dynamic_info_track(const file_info&)
{
}
void lastfm_tracker::on_volume_change(float)
{
}

void lastfm_tracker::submit_scrobble_if_needed()
{
    if (!m_is_playing || m_scrobble_sent || m_current.duration_seconds <= 0.0)
        return;

    if (!lastfm_is_authenticated())
        return;

    // Discard while suspended.
    if (lastfm_is_suspended())
    {
        return;
    }

    // Keep the existing rules (min length etc.).
    if (!m_rules.should_scrobble())
        return;

    // Additional guard: require enough *effective* listening time,
    // not just position (so seeking forward won't trigger scrobble).
    const double duration = m_current.duration_seconds;
    const double min_duration = 30.0;

    if (duration < min_duration)
        return;

    const double threshold = std::min(duration * 0.5, 240.0);
    const double listened = m_effective_listened_seconds;

    if (listened < threshold)
        return;

    LFM_DEBUG("Scrobble candidate.");

    // Eligibility uses effective listening, but playback_seconds here is
    // still the absolute position (not actually used for timestamp now
    // that we have a real start wallclock).
    const double played = m_playback_time;

    m_scrobble_sent = true;

    // Queue using the *real* playback start timestamp.
    lastfm_queue_scrobble_for_retry(m_current, played,
                                    /*refresh_on_submit=*/true, m_start_wallclock);
}

// Static factory registration
static play_callback_static_factory_t<lastfm_tracker> g_lastfm_tracker_factory;
