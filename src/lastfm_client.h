//
//  lastfm_client.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>
#include <ctime>
#include <vector>

#include "lastfm_auth_api.h"
#include "lastfm_track_info.h"
#include "lastfm_scrobble_result.h"
#include "lastfm_web_api.h"

class LastfmClient final : public ILastfmAuthApi
{
  public:
    LastfmClient() = default;

    // Auth state (from cfg)
    bool isAuthenticated() const;
    bool isSuspended() const;

    // Web API (thin wrappers)
    LastfmScrobbleResult updateNowPlaying(const LastfmTrackInfo& track, abort_callback& abort);
    LastfmScrobbleResult scrobble(const LastfmTrackInfo& track, double playbackSeconds, std::time_t startTimestamp,
                                  abort_callback& abort, LastfmTrackOutcome* outOutcome = nullptr);
    LastfmScrobbleResult scrobbleBatch(const std::vector<LastfmScrobbleRequest>& requests, abort_callback& abort,
                                       std::vector<LastfmTrackOutcome>* outPerTrack = nullptr);

    // ILastfmAuthApi
    bool startAuth(std::string& outUrl) override;
    bool completeAuth(LastfmAuthState& outState) override;
    void logout() override;
    bool hasPendingToken() const override;

  private:
    LastfmWebApi api;
};
