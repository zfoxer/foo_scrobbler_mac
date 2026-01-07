//
//  lastfm_scrobbler.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_scrobbler.h"
#include "lastfm_client.h"
#include "lastfm_ui.h"
#include "debug.h"

#include <foobar2000/SDK/main_thread_callback.h>
#include <foobar2000/SDK/popup_message.h>

#include <ctime>
#include <thread>
#include <chrono>

LastfmScrobbler::LastfmScrobbler(LastfmClient& client)
    : client(client), queue(client, [this]() { handleInvalidSessionOnce(); }),
      worker(client, queue,
             []
             {
                 LastfmWorker::Config c;
                 c.drainEnabled = &LastfmQueue::drainEnabled;
                 c.drainMinInterval = LastfmQueue::drainCooldown();
                 return c;
             }())
{
    queue.setShuttingDownFlag(&shuttingDown);
    worker.start();
    LFM_DEBUG("Startup: authenticated=" << (client.isAuthenticated() ? "yes" : "no")
                                        << " suspended=" << (client.isSuspended() ? "yes" : "no")
                                        << " pending=" << (unsigned)queue.getPendingScrobbleCount());
}

LastfmScrobbler::~LastfmScrobbler()
{
    shutdown();
}

void LastfmScrobbler::sendNowPlayingOnly(const LastfmTrackInfo& track)
{
    if (core_api::is_shutting_down() || shuttingDown.load(std::memory_order_acquire))
        return;

    if (!client.isAuthenticated() || client.isSuspended())
        return;

    if (lastfmDisableNowplaying())
        return;

    worker.postNowPlaying(track);
}

void LastfmScrobbler::dispatchRetryIfDue(const char* reasonTag)
{
    if (core_api::is_shutting_down() || shuttingDown.load(std::memory_order_acquire))
        return;

    if (!client.isAuthenticated() || client.isSuspended())
        return;

    const std::time_t now = std::time(nullptr);
    const std::size_t pending = queue.getPendingScrobbleCount();
    const bool due = pending > 0 ? queue.hasDueScrobble(now) : false;

    LFM_DEBUG("Dispatch gate (" << reasonTag << "): due=" << (due ? "yes" : "no") << " pending=" << (unsigned)pending);

    // Only dispatch if due and no worker is already running.
    if (due)
        worker.postDrain();
}

void LastfmScrobbler::onNowPlaying(const LastfmTrackInfo& track)
{
    if (core_api::is_shutting_down() || shuttingDown.load(std::memory_order_acquire))
        return;

    // Hard opt-out: NP disabled by prefs
    if (lastfmDisableNowplaying())
    {
        LFM_DEBUG("NowPlaying disabled by prefs.");
        return;
    }

    // Retry queue first (if authenticated), then do Now Playing.
    dispatchRetryIfDue("onNowPlaying");

    if (!client.isAuthenticated() || client.isSuspended())
        return;

    worker.postNowPlaying(track);
}

void LastfmScrobbler::refreshPendingMetadata(const LastfmTrackInfo& track)
{
    queue.refreshPendingScrobbleMetadata(track);
}

void LastfmScrobbler::queueScrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startWallclock,
                                    bool refreshOnSubmit)
{
    if (core_api::is_shutting_down() || shuttingDown.load(std::memory_order_acquire))
        return;

    if (!client.isAuthenticated() || client.isSuspended())
        return;

    queue.queueScrobbleForRetry(track, playbackSeconds, refreshOnSubmit, startWallclock);
}

void LastfmScrobbler::retryAsync()
{
    if (core_api::is_shutting_down())
        return;

    worker.postDrain();
}

void LastfmScrobbler::handleInvalidSessionOnce()
{
    if (core_api::is_shutting_down() || shuttingDown.load())
        return;

    if (invalidSessionHandled.exchange(true))
        return;

    LFM_INFO("Invalid session detected. Clearing auth once.");

    worker.postInvalidSession(); // block side-effects immediately (thread-safe)

    fb2k::inMainThread(
        [this]
        {
            if (shuttingDown.load())
                return;
            clearAuthentication();
            popup_message::g_show("Your Last.fm session is no longer valid.\n"
                                  "Please authenticate again from the Last.fm menu.",
                                  "Last.fm Scrobbler");
        });
}

void LastfmScrobbler::clearQueue()
{
    queue.clearAll();
}

void LastfmScrobbler::resetInvalidSessionHandling()
{
    invalidSessionHandled.store(false);
}

void LastfmScrobbler::onAuthenticationRecovered()
{
    // Lift invalid-session backoff ONLY
    resetInvalidSessionHandling();
    worker.postAuthRecovered();
}

void LastfmScrobbler::shutdown()
{
    // idempotent
    if (shuttingDown.exchange(true))
        return;

    worker.stop();
}
