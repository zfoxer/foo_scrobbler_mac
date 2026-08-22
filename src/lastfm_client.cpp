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

LastfmScrobbleResult LastfmClient::updateNowPlaying(const LastfmTrackInfo& track, abort_callback& abort)
{
    return api.updateNowPlaying(track, abort);
}

LastfmScrobbleResult LastfmClient::scrobble(const LastfmTrackInfo& track, double playbackSeconds,
                                            std::time_t startTimestamp, abort_callback& abort,
                                            LastfmTrackOutcome* outOutcome)
{
    return api.scrobble(track, playbackSeconds, startTimestamp, abort, outOutcome);
}

LastfmScrobbleResult LastfmClient::scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests,
                                                 abort_callback& abort, std::vector<LastfmTrackOutcome>* outPerTrack)
{
    return api.scrobbleBatch(requests, abort, outPerTrack);
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
    lastfmClearAuthentication();
}

bool LastfmClient::hasPendingToken() const
{
    return ::hasPendingToken();
}
