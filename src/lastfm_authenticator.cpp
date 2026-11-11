//
//  lastfm_authenticator.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_authenticator.h"
#include "lastfm_scrobbler.h"
#include "lastfm_state.h"
#include "debug.h"

#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/threadPool.h>

#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

static void openBrowserUrl(const std::string& url)
{
#if defined(__APPLE__)
    if (url.find('"') != std::string::npos)
        return;
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#elif defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__linux__)
    if (url.find('"') != std::string::npos)
        return;
    std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
    std::system(cmd.c_str());
#else
    LFM_INFO("Open manually: (url omitted)");
#endif
}

Authenticator::Authenticator(ILastfmAuthApi& api, LastfmScrobbler& scrobbler) : api(api), scrobbler(scrobbler)
{
}

bool Authenticator::startAuth(std::string& outUrl)
{
    return api.startAuth(outUrl);
}

LastfmSessionResult Authenticator::completeAuth(LastfmAuthState& outState)
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

void Authenticator::authenticateAsync()
{
    if (lastfmIsAuthenticated())
        return;

    if (busy.exchange(true))
    {
        LFM_DEBUG("Authentication request already in progress.");
        return;
    }

    const bool pending = hasPendingToken();
    status.store(pending ? FlowStatus::Completing : FlowStatus::Starting);

    fb2k::inWorkerThread(
        [this, pending]
        {
            status.store(pending ? completeFlow() : startFlow());
            busy.store(false);
        });
}

void Authenticator::clearAuthentication()
{
    scrobbler.resetInvalidSessionHandling();
    logout();
    status.store(FlowStatus::Cleared);
}

Authenticator::FlowStatus Authenticator::flowStatus() const
{
    return status.load();
}

bool Authenticator::flowBusy() const
{
    return busy.load();
}

bool Authenticator::openAuthorizationPage()
{
    if (!startAuth(authUrl) || authUrl.empty())
        return false;

    openBrowserUrl(authUrl);
    return true;
}

Authenticator::FlowStatus Authenticator::startFlow()
{
    if (openAuthorizationPage())
        return FlowStatus::WaitingForBrowser;

    logout();
    return FlowStatus::StartFailed;
}

Authenticator::FlowStatus Authenticator::completeFlow()
{
    LastfmAuthState state;
    switch (completeAuth(state))
    {
    case LastfmSessionResult::SUCCESS:
        scrobbler.onAuthenticated(state);
        return FlowStatus::Idle;
    case LastfmSessionResult::NOT_AUTHORIZED:
        openBrowserUrl(authUrl);
        return FlowStatus::NotGranted;
    case LastfmSessionResult::TOKEN_EXPIRED:
        logout();
        return openAuthorizationPage() ? FlowStatus::Expired : FlowStatus::Failed;
    case LastfmSessionResult::FAILED:
        break;
    }
    return FlowStatus::CompletionFailed;
}
