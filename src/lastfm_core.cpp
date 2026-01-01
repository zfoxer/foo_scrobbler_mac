//
//  lastfm_core.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_core.h"

LastfmCore& LastfmCore::instance()
{
    static LastfmCore g;
    return g;
}

LastfmCore::LastfmCore()
    : m_client(), m_scrobbler(m_client), m_authenticator(m_client) // LastfmClient implements ILastfmAuthApi
{
}
