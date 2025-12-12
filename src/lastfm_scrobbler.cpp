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

#include <thread>

LastfmScrobbler::LastfmScrobbler(LastfmClient& client)
    : client(client), queue(client, [this]() { handleInvalidSessionOnce(); })
{
}

void LastfmScrobbler::sendNowPlayingOnly(const LastfmTrackInfo& track)
{
    if (!client.isAuthenticated() || client.isSuspended())
        return;

    std::thread([this, track]() { client.updateNowPlaying(track); }).detach();
}

void LastfmScrobbler::onNowPlaying(const LastfmTrackInfo& track)
{
    if (client.isAuthenticated())
        queue.retryQueuedScrobblesAsync();

    if (!client.isAuthenticated() || client.isSuspended())
        return;

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
    queue.retryQueuedScrobblesAsync();
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
