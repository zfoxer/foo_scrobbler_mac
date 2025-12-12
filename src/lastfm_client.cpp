//
//  lastfm_client.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_client.h"

#include "lastfm_ui.h"
#include "lastfm_auth.h"

bool LastfmClient::isAuthenticated() const
{
    return ::isAuthenticated();
}

bool LastfmClient::isSuspended() const
{
    return ::isSuspended();
}

bool LastfmClient::updateNowPlaying(const LastfmTrackInfo& track)
{
    return api.updateNowPlaying(track);
}

LastfmScrobbleResult LastfmClient::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp)
{
    return api.scrobble(track, playbackSeconds, startTimestamp);
}

bool LastfmClient::startAuth(std::string& outUrl)
{
    return beginAuth(outUrl);
}

bool LastfmClient::completeAuth(LastfmAuthState& outState)
{
    return completeAuthFromCallbackUrl("", outState);
}

void LastfmClient::logout()
{
    clearAuthentication();
}

bool LastfmClient::hasPendingToken() const
{
    return ::hasPendingToken();
}
