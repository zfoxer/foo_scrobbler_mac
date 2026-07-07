//
//  lastfm_settings.h
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#pragma once

#include <string>

namespace lastfm::settings
{

enum ConsoleLevel
{
    ConsoleNone = 0,
    ConsoleBasic = 1,
    ConsoleDebug = 2
};

enum DynamicSourcesMode
{
    DynamicSourcesNone = 0,
    DynamicSourcesNowPlayingOnly = 1,
    DynamicSourcesNowPlayingAndScrobbling = 2
};

int consoleLevel();
void setConsoleLevel(int value);

bool showPlaybackMenu();
void setShowPlaybackMenu(bool enabled);

bool disableNowPlaying();
void setDisableNowPlaying(bool enabled);

bool onlyScrobbleFromMediaLibrary();
void setOnlyScrobbleFromMediaLibrary(bool enabled);

int configuredDynamicSourcesMode();
int effectiveDynamicSourcesMode();
void setDynamicSourcesMode(int value);

#define LASTFM_DECLARE_STRING_SETTING(getter, setter)                                                                  \
    std::string getter();                                                                                              \
    void setter(const std::string& value);

LASTFM_DECLARE_STRING_SETTING(artistTitleFormat, setArtistTitleFormat)
LASTFM_DECLARE_STRING_SETTING(albumArtistTitleFormat, setAlbumArtistTitleFormat)
LASTFM_DECLARE_STRING_SETTING(titleTitleFormat, setTitleTitleFormat)
LASTFM_DECLARE_STRING_SETTING(albumTitleFormat, setAlbumTitleFormat)

bool treatVariousArtistsAsEmpty();
void setTreatVariousArtistsAsEmpty(bool enabled);

LASTFM_DECLARE_STRING_SETTING(excludedArtistsPatternList, setExcludedArtistsPatternList)
LASTFM_DECLARE_STRING_SETTING(excludedTitlesPatternList, setExcludedTitlesPatternList)
LASTFM_DECLARE_STRING_SETTING(excludedAlbumsPatternList, setExcludedAlbumsPatternList)
LASTFM_DECLARE_STRING_SETTING(excludedTitleFormatExpression, setExcludedTitleFormatExpression)
LASTFM_DECLARE_STRING_SETTING(excludedGenreTemplateValueList, setExcludedGenreTemplateValueList)
LASTFM_DECLARE_STRING_SETTING(excludedMediaKindTemplateValueList, setExcludedMediaKindTemplateValueList)
LASTFM_DECLARE_STRING_SETTING(excludedPathTemplateValueList, setExcludedPathTemplateValueList)
LASTFM_DECLARE_STRING_SETTING(excludedCommentTemplateValueList, setExcludedCommentTemplateValueList)

#undef LASTFM_DECLARE_STRING_SETTING

} // namespace lastfm::settings
