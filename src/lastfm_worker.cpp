//
//  lastfm_worker.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_worker.h"
#include "debug.h"

#include <algorithm>
#include <ctime>

using namespace std::chrono;

LastfmWorker::LastfmWorker(LastfmClient& client, LastfmQueue& queue, Config cfg)
    : client_(client), queue_(queue), cfg_(cfg)
{
}

LastfmWorker::~LastfmWorker()
{
    stop();
}

void LastfmWorker::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return;

    stopRequested_.store(false);
    worker_ = std::thread([this]() { threadMain(); });
}

void LastfmWorker::stop()
{
    // Make shutdown visible ASAP (before any further wake/commands)
    shuttingDown_.store(true, std::memory_order_release);

    bool wasRunning = running_.exchange(false);
    if (!wasRunning)
        return;

    stopRequested_.store(true, std::memory_order_release);

    // Ensure Shutdown is enqueued even if queue is “full”
    enqueue(Command{CmdType::Shutdown, Clock::now()});
    wake();

    if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id())
        worker_.join();
}

void LastfmWorker::wake()
{
    cv_.notify_one();
}

void LastfmWorker::enqueue(const Command& cmd)
{
    std::lock_guard<std::mutex> lock(mtx_);

    // Backpressure: drop a low-value Drain if we are full
    if (cmds_.size() >= cfg_.maxPendingCommands)
    {
        if (cmd.type != CmdType::Shutdown)
        {
            auto it =
                std::find_if(cmds_.begin(), cmds_.end(), [](const Command& c) { return c.type == CmdType::Drain; });
            if (it != cmds_.end())
                cmds_.erase(it);

            if (cmds_.size() >= cfg_.maxPendingCommands)
                return; // still full, drop incoming command
        }
        // Shutdown always gets in
    }

    cmds_.push_back(cmd);
}

void LastfmWorker::postNowPlaying(const LastfmTrackInfo& track)
{
    if (shuttingDown_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire) ||
        stopRequested_.load())
        return;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        pendingNowPlaying_ = track;
    }
    wake();
}

void LastfmWorker::postDrain()
{
    if (shuttingDown_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire) ||
        stopRequested_.load())
        return;

    enqueue(Command{CmdType::Drain, Clock::now()});
    wake();
}

void LastfmWorker::postDrainAfter(std::chrono::milliseconds delay)
{
    if (shuttingDown_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire) ||
        stopRequested_.load())
        return;

    enqueue(Command{CmdType::Drain, Clock::now() + delay});
    wake();
}

void LastfmWorker::postAuthRecovered()
{
    if (shuttingDown_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire) ||
        stopRequested_.load())
        return;

    enqueue(Command{CmdType::AuthRecovered, Clock::now()});
    wake();
}

void LastfmWorker::postInvalidSession()
{
    // Block side-effects until auth is fixed. Drop pending NowPlaying.
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pendingNowPlaying_.reset();
    }
    authBlocked_.store(true);
}

void LastfmWorker::threadMain()
{
    LFM_DEBUG("LastfmWorker: started.");

    for (;;)
    {
        Command cmd{CmdType::Drain, Clock::now()};
        bool haveCmd = false;

        // earliest notBefore is timed wake
        Clock::time_point nextWake = Clock::time_point::max();

        {
            std::unique_lock<std::mutex> lock(mtx_);

            if (pendingNowPlaying_.has_value())
                nextWake = Clock::now();

            if (!cmds_.empty())
            {
                for (const auto& c : cmds_)
                    nextWake = std::min(nextWake, c.notBefore);
            }

            if (cmds_.empty() && !pendingNowPlaying_.has_value())
            {
                cv_.wait(lock, [this]()
                         { return stopRequested_.load() || !cmds_.empty() || pendingNowPlaying_.has_value(); });
            }
            else
            {
                cv_.wait_until(lock, nextWake, [this]()
                               { return stopRequested_.load() || !cmds_.empty() || pendingNowPlaying_.has_value(); });
            }

            // Pop first eligible command (notBefore <= now)
            const auto now = Clock::now();
            for (auto it = cmds_.begin(); it != cmds_.end(); ++it)
            {
                if (it->notBefore <= now)
                {
                    cmd = *it;
                    cmds_.erase(it);
                    haveCmd = true;
                    break;
                }
            }
        }

        // During shutdown: absolutely no side-effects (no NowPlaying, no drain, nothing)
        if (!shuttingDown_.load(std::memory_order_acquire))
        {
            // Always try NowPlaying when we wake (respect debounce)
            handleNowPlayingIfReady();
        }

        if (!haveCmd)
        {
            if (stopRequested_.load())
                break;
            continue;
        }

        if (cmd.type == CmdType::Shutdown)
            break;

        handle(cmd);

        if (stopRequested_.load())
            break;
    }

    LFM_DEBUG("LastfmWorker: stopped.");
}

void LastfmWorker::handle(const Command& cmd)
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;

    switch (cmd.type)
    {
    case CmdType::Drain:
        handleDrain();
        break;

    case CmdType::AuthRecovered:
        authBlocked_.store(false);
        handleDrain();
        break;

    case CmdType::Shutdown:
        break;
    }
}

void LastfmWorker::handleNowPlayingIfReady()
{
    std::optional<LastfmTrackInfo> t;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!pendingNowPlaying_.has_value())
            return;

        const auto now = Clock::now();
        if (lastNowPlayingSent_ != Clock::time_point::min() && now - lastNowPlayingSent_ < cfg_.nowPlayingMinInterval)
            return;

        t = *pendingNowPlaying_;
        pendingNowPlaying_.reset();
        lastNowPlayingSent_ = now;
    }

    if (!t.has_value())
        return;

    if (authBlocked_.load())
        return;

    if (!client_.isAuthenticated() || client_.isSuspended())
        return;

    if (t->artist.empty() || t->title.empty())
        return;

    (void)client_.updateNowPlaying(*t);
}

void LastfmWorker::handleDrain()
{
    if (shuttingDown_.load(std::memory_order_acquire))
        return;

    if (cfg_.drainEnabled && cfg_.drainEnabled() == false)
        return;

    if (authBlocked_.load())
        return;

    if (!client_.isAuthenticated() || client_.isSuspended())
        return;

    const auto now = Clock::now();

    const std::size_t pending0 = queue_.getPendingScrobbleCount();
    const bool enforceCooldown = pending0 >= COOLDOWN_LIMIT;

    if (enforceCooldown)
    {
        if (now - lastDrain_ < cfg_.drainMinInterval)
            return;
        lastDrain_ = now;
    }

    // Drain only if something is due
    const std::time_t nowWall = std::time(nullptr);
    if (pending0 == 0)
        return;
    if (!queue_.hasDueScrobble(nowWall))
        return;

    const auto budgetEnd = Clock::now() + cfg_.drainBudget;

    while (Clock::now() < budgetEnd)
    {
        if (shuttingDown_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire))
            break;

        queue_.retryQueuedScrobbles();

        if (shuttingDown_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire))
            break;

        if (queue_.getPendingScrobbleCount() == 0)
            break;

        if (!queue_.hasDueScrobble(std::time(nullptr)))
            break;

        std::this_thread::sleep_for(cfg_.drainStepSleep);
    }

    // If still due, schedule a follow-up soon (paced)
    if (!shuttingDown_.load(std::memory_order_acquire) && !stopRequested_.load(std::memory_order_acquire) &&
        queue_.getPendingScrobbleCount() > 0 && queue_.hasDueScrobble(std::time(nullptr)))
    {
        postDrainAfter(std::chrono::milliseconds(250));
    }
}
