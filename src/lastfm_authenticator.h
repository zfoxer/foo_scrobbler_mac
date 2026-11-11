//
//  lastfm_authenticator.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>
#include <string>

#include "lastfm_auth_api.h"

class LastfmScrobbler;

class Authenticator
{
  public:
    enum class FlowStatus
    {
        Idle,
        Starting,
        WaitingForBrowser,
        Completing,
        StartFailed,
        NotGranted,
        Expired,
        CompletionFailed,
        Failed,
        Cleared
    };

    Authenticator(ILastfmAuthApi& api, LastfmScrobbler& scrobbler);

    bool startAuth(std::string& outUrl);
    LastfmSessionResult completeAuth(LastfmAuthState& outState);
    void logout();
    bool hasPendingToken() const;

    void authenticateAsync();
    void clearAuthentication();
    FlowStatus flowStatus() const;
    bool flowBusy() const;

  private:
    bool openAuthorizationPage();
    FlowStatus startFlow();
    FlowStatus completeFlow();

  private:
    ILastfmAuthApi& api;
    LastfmScrobbler& scrobbler;
    std::atomic<bool> busy{false};
    std::atomic<FlowStatus> status{FlowStatus::Idle};
    std::string authUrl;
};

Authenticator& lastfmAuthenticator();
