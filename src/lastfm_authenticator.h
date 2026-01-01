//
//  lastfm_authenticator.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

#include "lastfm_auth_api.h"
#include "lastfm_types.h"

class Authenticator
{
  public:
    explicit Authenticator(ILastfmAuthApi& api);

    bool startAuth(std::string& outUrl);
    bool completeAuth(LastfmAuthState& outState);
    void logout();
    bool hasPendingToken() const;

  private:
    ILastfmAuthApi& api;
};
