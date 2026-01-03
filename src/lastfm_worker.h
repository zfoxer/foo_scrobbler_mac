//
//  lastfm_worker.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include "lastfm_client.h"
#include "lastfm_queue.h"
#include "lastfm_track_info.h"

// Single-worker actor: all Last.fm side-effects (network + draining) run on one dedicated thread.
// Callers only post commands; no detached threads in the worker.
class LastfmWorker
{
  public:
    static const int COOLDOWN_LIMIT = 101;
    using Clock = std::chrono::steady_clock;
    bool isShuttingDown() const noexcept
    {
        return shuttingDown_.load(std::memory_order_acquire);
    }

    struct Config
    {
        std::size_t maxPendingCommands;
        bool coalesceNowPlaying;
        std::chrono::milliseconds nowPlayingMinInterval;
        std::chrono::milliseconds drainMinInterval;
        std::chrono::milliseconds drainBudget;
        std::chrono::milliseconds drainStepSleep;
        std::function<bool()> drainEnabled;

        Config() noexcept
            : maxPendingCommands(2048), coalesceNowPlaying(true), nowPlayingMinInterval(1500), drainMinInterval(250),
              drainBudget(1200), drainStepSleep(10)
        {
        }
    };

    explicit LastfmWorker(LastfmClient& client, LastfmQueue& queue, Config cfg = Config());
    ~LastfmWorker();

    LastfmWorker(const LastfmWorker&) = delete;
    LastfmWorker& operator=(const LastfmWorker&) = delete;

    // Lifecycle
    void start();
    void stop(); // idempotent, joins worker thread

    // Thread-safe entry points
    void postNowPlaying(const LastfmTrackInfo& track);
    void postDrain();
    void postDrainAfter(std::chrono::milliseconds delay);
    void postAuthRecovered();

    // Called when INVALID_SESSION is detected (clears auth). Blocks worker side-effects until recovered.
    void postInvalidSession();

  private:
    enum class CmdType : std::uint8_t
    {
        Drain,
        AuthRecovered,
        Shutdown
    };

    struct Command
    {
        CmdType type;
        Clock::time_point notBefore;
    };

    void threadMain();
    void enqueue(const Command& cmd);
    void wake();

    // Worker-thread only
    void handle(const Command& cmd);
    void handleNowPlayingIfReady();
    void handleDrain();

    std::atomic<bool> shuttingDown_{false};
    LastfmClient& client_;
    LastfmQueue& queue_;
    Config cfg_;

    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Command> cmds_;

    // Coalesced NowPlaying state (latest wins)
    std::optional<LastfmTrackInfo> pendingNowPlaying_;
    Clock::time_point lastNowPlayingSent_{Clock::time_point::min()};

    // Drain pacing & auth gate
    Clock::time_point lastDrain_{Clock::time_point::min()};
    std::atomic<bool> authBlocked_{false};

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
};
