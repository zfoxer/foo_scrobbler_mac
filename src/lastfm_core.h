//
//  lastfm_core.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include "lastfm_client.h"
#include "lastfm_scrobbler.h"
#include "lastfm_authenticator.h"

// Wiring-only singleton (module lifetime). No logic.
class LastfmCore
{
  public:
    static LastfmCore& instance();

    LastfmClient& client()
    {
        return m_client;
    }
    LastfmScrobbler& scrobbler()
    {
        return m_scrobbler;
    }
    Authenticator& authenticator()
    {
        return m_authenticator;
    }

  private:
    LastfmCore();

  private:
    LastfmClient m_client;
    LastfmScrobbler m_scrobbler;
    Authenticator m_authenticator;
};
