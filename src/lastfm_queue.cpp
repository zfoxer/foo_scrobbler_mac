//
//  lastfm_queue.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos.
//

#include "lastfm_queue.h"
#include "lastfm_scrobble.h"
#include "lastfm_auth.h"
#include "lastfm_ui.h"
#include "lastfm_tracker.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>

#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{

// Persistent queue storage
static const GUID guid_cfg_lastfm_pending_scrobbles = {
    0x9b3b2c41, 0x4c2d, 0x4d8f, {0x9a, 0xa1, 0x1e, 0x37, 0x5b, 0x6a, 0x82, 0x19}};

static cfg_string cfg_lastfm_pending_scrobbles(guid_cfg_lastfm_pending_scrobbles, "");

// Simple line-based format:
// artist \t title \t album \t duration_seconds \t playback_seconds \t start_timestamp \t refresh_on_submit
struct queued_scrobble
{
    std::string artist;
    std::string title;
    std::string album;
    double duration_seconds = 0.0;
    double playback_seconds = 0.0;
    std::time_t start_timestamp = 0;
    bool refresh_on_submit = false;
};

static std::string serialize_scrobble(const queued_scrobble& q)
{
    std::string out;
    out.reserve(192);
    out += q.artist;
    out += '\t';
    out += q.title;
    out += '\t';
    out += q.album;
    out += '\t';
    out += std::to_string(q.duration_seconds);
    out += '\t';
    out += std::to_string(q.playback_seconds);
    out += '\t';
    out += std::to_string(static_cast<long long>(q.start_timestamp));
    out += '\t';
    out += (q.refresh_on_submit ? "1" : "0");
    return out;
}

static bool parse_double(const char* s, double& out)
{
    if (!s || !*s)
        return false;

    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s)
        return false;

    out = v;
    return true;
}

static std::vector<queued_scrobble> load_pending_scrobbles_impl()
{
    std::vector<queued_scrobble> result;

    pfc::string8 raw = cfg_lastfm_pending_scrobbles.get();
    const char* data = raw.c_str();
    if (!data || !*data)
        return result;

    const char* line_start = data;
    while (*line_start)
    {
        const char* line_end = std::strchr(line_start, '\n');
        std::string line;

        if (line_end)
        {
            line.assign(line_start, line_end - line_start);
            line_start = line_end + 1;
        }
        else
        {
            line.assign(line_start);
            line_start += line.size();
        }

        if (line.empty())
            continue;

        std::vector<std::string> parts;
        std::size_t pos = 0;

        while (true)
        {
            std::size_t tab = line.find('\t', pos);
            if (tab == std::string::npos)
            {
                parts.push_back(line.substr(pos));
                break;
            }
            parts.push_back(line.substr(pos, tab - pos));
            pos = tab + 1;
        }

        if (parts.size() < 5)
            continue;

        queued_scrobble q;
        q.artist = parts[0];
        q.title = parts[1];
        q.album = parts[2];

        double dur = 0.0;
        double pb = 0.0;

        if (!parse_double(parts[3].c_str(), dur))
            dur = 0.0;
        if (!parse_double(parts[4].c_str(), pb))
            pb = 0.0;

        q.duration_seconds = dur;
        q.playback_seconds = pb;

        // Optional 6th field: original start timestamp (absolute, seconds since epoch)
        q.start_timestamp = 0;
        if (parts.size() >= 6)
        {
            char* end = nullptr;
            long long ts = std::strtoll(parts[5].c_str(), &end, 10);
            if (end != parts[5].c_str())
                q.start_timestamp = static_cast<std::time_t>(ts);
        }

        // Optional 7th field: refresh_on_submit flag
        q.refresh_on_submit = false;
        if (parts.size() >= 7)
        {
            q.refresh_on_submit = (parts[6] == "1");
        }

        result.push_back(q);
    }

    return result;
}

static void save_pending_scrobbles_impl(const std::vector<queued_scrobble>& items)
{
    pfc::string8 raw;

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& q = items[i];
        std::string line = serialize_scrobble(q);
        raw += line.c_str();
        raw += "\n";
    }

    cfg_lastfm_pending_scrobbles.set(raw);
}

} // anonymous namespace

//
// lastfm_queue implementation
//

lastfm_queue& lastfm_queue::instance()
{
    static lastfm_queue s_instance;
    return s_instance;
}

void lastfm_queue::refresh_pending_scrobble_metadata(const lastfm_track_info& track)
{
    std::vector<queued_scrobble> items = load_pending_scrobbles_impl();
    if (items.empty())
        return;

    bool updated = false;

    // Walk from the end so we hit the most recent one first.
    for (auto it = items.rbegin(); it != items.rend(); ++it)
    {
        if (it->refresh_on_submit)
        {
            it->artist = track.artist;
            it->title = track.title;
            it->album = track.album;
            it->duration_seconds = track.duration_seconds;
            updated = true;
            break;
        }
    }

    if (updated)
    {
        save_pending_scrobbles_impl(items);
        LFM_DEBUG("Updated pending scrobble metadata to latest tags: " << track.artist.c_str() << " - "
                                                                       << track.title.c_str());
    }
}

void lastfm_queue::queue_scrobble_for_retry(const lastfm_track_info& track, double playback_seconds,
                                            bool refresh_on_submit, std::time_t start_timestamp)
{
    if (track.title.empty() || track.artist.empty())
    {
        LFM_INFO("Missing track info, not submitting.");
        return;
    }

    queued_scrobble q;
    q.artist = track.artist;
    q.title = track.title;
    q.album = track.album;
    q.duration_seconds = track.duration_seconds;
    q.playback_seconds = playback_seconds;
    q.start_timestamp = start_timestamp;
    q.refresh_on_submit = refresh_on_submit;

    std::vector<queued_scrobble> items = load_pending_scrobbles_impl();
    items.push_back(q);
    save_pending_scrobbles_impl(items);

    LFM_DEBUG("Queued scrobble for retry: " << track.artist.c_str() << " - " << track.title.c_str()
                                            << (refresh_on_submit ? " (refresh_on_submit)" : ""));
}

void lastfm_queue::retry_queued_scrobbles()
{
    std::vector<queued_scrobble> items = load_pending_scrobbles_impl();
    if (items.empty())
        return;

    if ((unsigned)items.size() > 1)
        LFM_INFO("Submitting " << (unsigned)items.size() << " queued scrobble(s).");

    std::vector<queued_scrobble> remaining;
    bool invalid_session_seen = false;

    for (const auto& q : items)
    {
        if (invalid_session_seen)
        {
            remaining.push_back(q);
            continue;
        }

        lastfm_auth_state auth_state = lastfm_get_auth_state();
        if (!auth_state.is_authenticated || auth_state.session_key.empty())
        {
            LFM_INFO("Queue retry: no valid auth state, keeping remaining items.");
            remaining.push_back(q);
            invalid_session_seen = true;
            continue;
        }

        lastfm_track_info t;
        t.artist = q.artist;
        t.title = q.title;
        t.album = q.album;
        t.mbid.clear();
        t.duration_seconds = q.duration_seconds;

        lastfm_scrobble_result res = lastfm_scrobble_track(t, q.playback_seconds, q.start_timestamp);

        switch (res)
        {
        case lastfm_scrobble_result::success:
        {
            LFM_DEBUG("Queued scrobble succeeded: " << t.artist.c_str() << " - " << t.title.c_str());
            break;
        }

        case lastfm_scrobble_result::temporary_error:
        case lastfm_scrobble_result::other_error:
            remaining.push_back(q);
            break;

        case lastfm_scrobble_result::invalid_session:
        {
            remaining.push_back(q);
            invalid_session_seen = true;

            LFM_INFO("Queue retry: invalid session detected. Clearing auth on main thread.");

            fb2k::inMainThread(
                []()
                {
                    lastfm_clear_authentication();
                    popup_message::g_show("Your Last.fm session is no longer valid.\n"
                                          "Please authenticate again from the Last.fm menu.",
                                          "Last.fm Scrobbler");
                });
            break;
        }
        }
    }

    save_pending_scrobbles_impl(remaining);

    if ((unsigned)remaining.size())
        LFM_INFO("Pending scrobbles remaining: " << (unsigned)remaining.size());
}

void lastfm_queue::retry_queued_scrobbles_async()
{
    std::thread([]() { lastfm_queue::instance().retry_queued_scrobbles(); }).detach();
}

std::size_t lastfm_queue::get_pending_scrobble_count() const
{
    return load_pending_scrobbles_impl().size();
}
