//
//  lastfm_tracker.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <foobar2000/SDK/foobar2000.h>

#include <ctime>
#include <string>

#include "lastfm_rules.h"
#include "lastfm_track_info.h"

class LastfmTracker : public play_callback_static
{
  public:
    unsigned get_flags() override;
    void on_playback_starting(play_control::t_track_command command, bool paused) override;
    void on_playback_new_track(metadb_handle_ptr track) override;
    void on_playback_stop(play_control::t_stop_reason reason) override;
    void on_playback_seek(double time) override;
    void on_playback_pause(bool paused) override;
    void on_playback_edited(metadb_handle_ptr track) override;
    void on_playback_dynamic_info(const file_info& info) override;
    void on_playback_dynamic_info_track(const file_info& info) override;
    void on_playback_time(double time) override;
    void on_volume_change(float volume) override;

    void queuePendingAtShutdown();

  private:
    void fillTrackInfoFromTf(const metadb_handle_ptr& track, LastfmTrackInfo& out);
    void recompileTfIfNeeded();
    void resetState();
    void resetLocalChannelState();
    void resetDynamicChannelState();
    void submitLocalScrobbleIfNeeded(bool allowFilterRecovery);
    void updateFromTrack(const metadb_handle_ptr& track);
    void handleDynamicStreamUpdate(const file_info& info);
    void applyTitleFormatToStreamMetadata(const file_info& info, std::string& artist, std::string& title,
                                          std::string& album);
    void refreshCurrentFileMetadata(bool allowDispatch);
    bool refreshFooScrobblerTagAllows();
    void resendNowPlayingAfterResume();
    void maybeCacheDynamicScrobble(bool allowFilterRecovery);
    bool trackIsExcluded(const LastfmTrackInfo& track, const file_info* externalInfo = nullptr);
    bool currentTrackIsExcluded(const file_info* externalInfo = nullptr);

    enum class PlaybackChannel
    {
        None,
        LocalFile,
        DynamicStream
    };

    enum class LocalScrobbleState
    {
        Idle,
        Tracking,
        WaitingForMetadata,
        WaitingForFilterRecovery,
        BlockedByExclusionFilters,
        DeferredUntilBoundary,
        Submitted
    };

    enum class DynamicSegmentState
    {
        Inactive,
        WaitingForMetadata,
        Tracking,
        WaitingForFilterRecovery
    };

    enum class DynamicPendingState
    {
        Empty,
        Cached
    };

    struct ListenClock
    {
        double effectiveSeconds = 0.0;
        double lastReportedTime = 0.0;
        bool haveLastReportedTime = false;
    };

    struct LocalChannelState
    {
        LocalScrobbleState state = LocalScrobbleState::Idle;
        ListenClock clock;
    };

    struct DynamicChannelState
    {
        DynamicSegmentState segmentState = DynamicSegmentState::Inactive;
        DynamicPendingState pendingState = DynamicPendingState::Empty;
        ListenClock clock;
        // Preserve stream callback metadata so TF exclusion recovery sees the same fields as the original update.
        bool haveCurrentSegmentInfo = false;
        file_info_impl currentSegmentInfo;
        LastfmTrackInfo pendingTrack{};
        bool havePendingSegmentInfo = false;
        file_info_impl pendingSegmentInfo;
        double pendingPlaybackTime = 0.0;
        std::time_t pendingStartWallclock = 0;
        std::time_t segmentStartWallclock = 0;
        std::string dedupLastPath;
        std::string dedupLastArtist;
        std::string dedupLastTitle;
    };

    void updateListeningClock(ListenClock& clock, double time, bool blocked);

    std::time_t startWallclock = 0;
    bool isPlaying = false;
    double playbackTime = 0.0;
    PlaybackChannel channel = PlaybackChannel::None;
    bool currentFooScrobblerTagAllows = true;
    bool fooScrobblerTagBlockLogged = false;
    bool wasSuspended = false;

    LastfmTrackInfo current;

    LocalChannelState local;

    metadb_handle_ptr currentHandle;
    LastfmRules rules;

    service_ptr_t<titleformat_object> artistTf_;
    service_ptr_t<titleformat_object> albumArtistTf_;
    service_ptr_t<titleformat_object> titleTf_;
    service_ptr_t<titleformat_object> albumTf_;
    service_ptr_t<titleformat_object> mbidTf_;
    service_ptr_t<titleformat_object> fallbackArtistTf_;

    std::string cachedArtistTfExpr_;
    std::string cachedAlbumArtistTfExpr_;
    std::string cachedTitleTfExpr_;
    std::string cachedAlbumTfExpr_;
    std::string cachedMbidTfExpr_;

    // Dynamic stream scrobble (network sources only)
    DynamicChannelState dynamic;

    // Helpers (network-only)
    void startDynamicSegment();
    void submitDynamicPendingIfAny();
};
