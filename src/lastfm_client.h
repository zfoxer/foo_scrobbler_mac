//
//  lastfm_client.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>
#include <ctime>

#include "lastfm_auth_api.h"
#include "lastfm_track_info.h"
#include "lastfm_results.h"
#include "lastfm_web_api.h"

class LastfmClient final : public ILastfmAuthApi
{
  public:
    LastfmClient() = default;

    // Auth state (from cfg via lastfm_ui)
    bool isAuthenticated() const;
    bool isSuspended() const;

    // Web API (thin wrappers)
    bool updateNowPlaying(const LastfmTrackInfo& track);
    LastfmScrobbleResult scrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startTimestamp);

    // ILastfmAuthApi
    bool startAuth(std::string& outUrl) override;
    bool completeAuth(LastfmAuthState& outState) override;
    void logout() override;
    bool hasPendingToken() const override;

  private:
    LastfmWebApi api;
};
