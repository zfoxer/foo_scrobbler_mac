//
//  lastfm_scrobbler.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_scrobbler.h"
#include "lastfm_client.h"
#include "lastfm_ui.h"
#include "debug.h"

#include <ctime>
#include <thread>
#include <chrono>

LastfmScrobbler::LastfmScrobbler(LastfmClient& client)
    : client(client), queue(client, [this]() { handleInvalidSessionOnce(); })
{
    LFM_DEBUG("Startup: authenticated=" << (client.isAuthenticated() ? "yes" : "no")
                                        << " suspended=" << (client.isSuspended() ? "yes" : "no")
                                        << " pending=" << (unsigned)queue.getPendingScrobbleCount());
}

void LastfmScrobbler::sendNowPlayingOnly(const LastfmTrackInfo& track)
{
    if (!client.isAuthenticated() || client.isSuspended())
        return;

    if (lastfm_disable_nowplaying())
        return;

    std::thread([this, track]() { client.updateNowPlaying(track); }).detach();
}

void LastfmScrobbler::dispatchRetryIfDue(const char* reasonTag)
{
    if (!client.isAuthenticated() || client.isSuspended())
        return;

    const std::time_t now = std::time(nullptr);
    const std::size_t pending = queue.getPendingScrobbleCount();
    const bool due = pending > 0 ? queue.hasDueScrobble(now) : false;
    const bool inflight = queue.isRetryInFlight();

    LFM_DEBUG("Dispatch gate (" << reasonTag << "): due=" << (due ? "yes" : "no") << " pending=" << (unsigned)pending
                                << " inflight=" << (inflight ? "yes" : "no"));

    // Only dispatch if due and no worker is already running.
    if (due && !inflight)
        queue.retryQueuedScrobblesAsync();
}

void LastfmScrobbler::onNowPlaying(const LastfmTrackInfo& track)
{
    // Hard opt-out: NP disabled by prefs
    if (lastfm_disable_nowplaying())
    {
        LFM_DEBUG("NowPlaying disabled by prefs.");
        return;
    }

    // Retry queue first (if authenticated), then do Now Playing.
    dispatchRetryIfDue("onNowPlaying");

    if (!client.isAuthenticated() || client.isSuspended())
        return;

    // Avoid overlapping HTTP traffic: don't send NowPlaying while a retry worker is in flight.
    if (queue.isRetryInFlight())
    {
        LFM_DEBUG("NowPlaying: retry in flight, deferring.");
        deferNowPlayingAfterRetry(track);
        return;
    }

    sendNowPlayingOnly(track);
}

void LastfmScrobbler::refreshPendingMetadata(const LastfmTrackInfo& track)
{
    queue.refreshPendingScrobbleMetadata(track);
}

void LastfmScrobbler::queueScrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startWallclock,
                                    bool refreshOnSubmit)
{
    if (!client.isAuthenticated() || client.isSuspended())
        return;

    queue.queueScrobbleForRetry(track, playbackSeconds, refreshOnSubmit, startWallclock);
}

void LastfmScrobbler::retryAsync()
{
    dispatchRetryIfDue("retryAsync");
}

void LastfmScrobbler::handleInvalidSessionOnce()
{
    if (invalidSessionHandled)
        return;

    invalidSessionHandled = true;

    LFM_INFO("Invalid session detected. Clearing auth once.");

    clearAuthentication();
    popup_message::g_show("Your Last.fm session is no longer valid.\n"
                          "Please authenticate again from the Last.fm menu.",
                          "Last.fm Scrobbler");
}

void LastfmScrobbler::clearQueue()
{
    queue.clearAll();
}

void LastfmScrobbler::resetInvalidSessionHandling()
{
    invalidSessionHandled = false;
}

void LastfmScrobbler::deferNowPlayingAfterRetry(const LastfmTrackInfo& track)
{
    const uint64_t epoch = nowPlayingEpoch.fetch_add(1) + 1;

    {
        std::lock_guard<std::mutex> lock(nowPlayingMutex);
        deferredNowPlayingTrack = track;
    }

    std::thread(
        [this, epoch]()
        {
            // Wait until retry finishes (bounded wait).
            for (int i = 0; i < 1000; ++i) // ~10 seconds at 10ms
            {
                if (!queue.isRetryInFlight())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Only the latest deferred request should fire.
            if (nowPlayingEpoch.load() != epoch)
                return;

            if (!client.isAuthenticated() || client.isSuspended())
                return;

            LastfmTrackInfo t;
            {
                std::lock_guard<std::mutex> lock(nowPlayingMutex);
                t = deferredNowPlayingTrack;
            }

            // Final sanity: Don't send garbage NP.
            if (t.artist.empty() || t.title.empty())
                return;

            LFM_DEBUG("NowPlaying: sending deferred update after retry.");
            sendNowPlayingOnly(t);
        })
        .detach();
}
