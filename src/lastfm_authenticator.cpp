//
//  lastfm_authenticator.cpp
//  foo_scrobbler_mac
//
//  (c) 2025 by Konstantinos Kyriakopoulos
//

#include "lastfm_authenticator.h"

Authenticator::Authenticator(ILastfmAuthApi& api) : api(api)
{
}

bool Authenticator::startAuth(std::string& outUrl)
{
    return api.startAuth(outUrl);
}

bool Authenticator::completeAuth(LastfmAuthState& outState)
{
    return api.completeAuth(outState);
}

void Authenticator::logout()
{
    api.logout();
}

bool Authenticator::hasPendingToken() const
{
    return api.hasPendingToken();
}
