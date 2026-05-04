//
//  lastfm_client.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_client.h"
#include "lastfm_auth.h"
#include "lastfm_state.h"

bool LastfmClient::isSuspended() const
{
    return lastfmIsSuspended();
}

bool LastfmClient::isAuthenticated() const
{
    return lastfmIsAuthenticated();
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

LastfmScrobbleResult LastfmClient::scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests)
{
    return api.scrobbleBatch(requests);
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
