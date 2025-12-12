//
//  lastfm_auth_api.h
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>
#include "lastfm_types.h"

class ILastfmAuthApi
{
  public:
    virtual ~ILastfmAuthApi() = default;

    virtual bool startAuth(std::string& outUrl) = 0;
    virtual bool completeAuth(LastfmAuthState& outState) = 0;
    virtual void logout() = 0;
    virtual bool hasPendingToken() const = 0;
};
