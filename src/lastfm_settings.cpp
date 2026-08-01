//
//  lastfm_settings.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_settings.h"

#include <foobar2000/SDK/foobar2000.h>

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace
{
constexpr const char* kConsoleNone = "foo_scrobbler.console.no";
constexpr const char* kConsoleBasic = "foo_scrobbler.console.basic";
constexpr const char* kConsoleDebug = "foo_scrobbler.console.debug";

constexpr const char* kShowPlaybackMenu = "foo_scrobbler.menu.show_playback_menu";

constexpr const char* kDisableNowPlaying = "foo_scrobbler.scrobbling.disable_nowplaying";
constexpr const char* kOnlyFromLibrary = "foo_scrobbler.scrobbling.only_from_library";

constexpr const char* kDynamicNone = "foo_scrobbler.dynamic.no";
constexpr const char* kDynamicNowPlayingOnly = "foo_scrobbler.dynamic.np_only";
constexpr const char* kDynamicNowPlayingAndScrobbling = "foo_scrobbler.dynamic.np_and_scrobble";

constexpr const char* kTreatVariousArtistsAsEmpty = "foo_scrobbler.tags.compilation.treat_va_empty";

constexpr const char* kArtistTf = "foo_scrobbler.tf.artist";
constexpr const char* kAlbumArtistTf = "foo_scrobbler.tf.album_artist";
constexpr const char* kTitleTf = "foo_scrobbler.tf.title";
constexpr const char* kAlbumTf = "foo_scrobbler.tf.album";
constexpr const char* kMbidTf = "foo_scrobbler.tf.mbid";

constexpr const char* kExcludeArtists = "foo_scrobbler.scrobbling.exclude_artists";
constexpr const char* kExcludeTitles = "foo_scrobbler.scrobbling.exclude_titles";
constexpr const char* kExcludeAlbums = "foo_scrobbler.scrobbling.exclude_albums";
constexpr const char* kExcludeTf = "foo_scrobbler.scrobbling.exclude_tf";
constexpr const char* kExcludeTemplateGenre = "foo_scrobbler.scrobbling.exclude_template_genre";
constexpr const char* kExcludeTemplateMediaKind = "foo_scrobbler.scrobbling.exclude_template_media_kind";
constexpr const char* kExcludeTemplatePath = "foo_scrobbler.scrobbling.exclude_template_path";
constexpr const char* kExcludeTemplateComment = "foo_scrobbler.scrobbling.exclude_template_comment";

struct BooleanRadioOption
{
    const char* key;
    bool defaultOn;
};

const BooleanRadioOption kConsoleOptions[] = {
    {kConsoleNone, false},
    {kConsoleBasic, true},
    {kConsoleDebug, false},
};

const BooleanRadioOption kDynamicOptions[] = {
    {kDynamicNone, false},
    {kDynamicNowPlayingOnly, false},
    {kDynamicNowPlayingAndScrobbling, true},
};

int clampChoice(int value)
{
    return std::clamp(value, 0, 2);
}

bool getBool(const char* key, bool defaultValue)
{
    return fb2k::configStore::get()->getConfigBool(key, defaultValue);
}

void setBool(const char* key, bool value)
{
    fb2k::configStore::get()->setConfigBool(key, value);
}

std::string getString(const char* key, const char* defaultValue)
{
    const auto value = fb2k::configStore::get()->getConfigString(key, defaultValue);
    return std::string(value->c_str());
}

void setString(const char* key, const std::string& value)
{
    fb2k::configStore::get()->setConfigString(key, value.c_str());
}

void setBooleanRadioChoice(const BooleanRadioOption* options, std::size_t count, int value)
{
    value = clampChoice(value);

    auto api = fb2k::configStore::get();
    const auto transaction = api->acquireTransactionScope();
    (void)transaction;

    for (std::size_t i = 0; i < count; ++i)
        api->setConfigBool(options[i].key, static_cast<int>(i) == value);
}

int booleanRadioChoice(const BooleanRadioOption* options, std::size_t count, int defaultIndex)
{
    int firstOn = -1;
    int onCount = 0;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (getBool(options[i].key, options[i].defaultOn))
        {
            if (firstOn < 0)
                firstOn = static_cast<int>(i);
            ++onCount;
        }
    }

    if (onCount == 0)
    {
        setBooleanRadioChoice(options, count, defaultIndex);
        return defaultIndex;
    }

    if (onCount > 1)
        setBooleanRadioChoice(options, count, firstOn);

    return firstOn;
}
} // namespace

namespace lastfm::settings
{

int consoleLevel()
{
    return booleanRadioChoice(kConsoleOptions, std::size(kConsoleOptions), ConsoleBasic);
}

void setConsoleLevel(int value)
{
    setBooleanRadioChoice(kConsoleOptions, std::size(kConsoleOptions), value);
}

bool showPlaybackMenu()
{
    return getBool(kShowPlaybackMenu, true);
}

void setShowPlaybackMenu(bool enabled)
{
    setBool(kShowPlaybackMenu, enabled);
}

bool disableNowPlaying()
{
    return getBool(kDisableNowPlaying, false);
}

void setDisableNowPlaying(bool enabled)
{
    setBool(kDisableNowPlaying, enabled);
}

bool onlyScrobbleFromMediaLibrary()
{
    return getBool(kOnlyFromLibrary, false);
}

void setOnlyScrobbleFromMediaLibrary(bool enabled)
{
    setBool(kOnlyFromLibrary, enabled);
}

int configuredDynamicSourcesMode()
{
    return booleanRadioChoice(kDynamicOptions, std::size(kDynamicOptions), DynamicSourcesNowPlayingAndScrobbling);
}

int effectiveDynamicSourcesMode()
{
    if (onlyScrobbleFromMediaLibrary())
        return DynamicSourcesNone;
    return configuredDynamicSourcesMode();
}

void setDynamicSourcesMode(int value)
{
    setBooleanRadioChoice(kDynamicOptions, std::size(kDynamicOptions), value);
}

#define LASTFM_STRING_SETTINGS(X)                                                                                      \
    X(artistTitleFormat, setArtistTitleFormat, kArtistTf, "[%Artist%]")                                                \
    X(albumArtistTitleFormat, setAlbumArtistTitleFormat, kAlbumArtistTf, "[%Album Artist%]")                           \
    X(titleTitleFormat, setTitleTitleFormat, kTitleTf, "[%Title%]")                                                    \
    X(albumTitleFormat, setAlbumTitleFormat, kAlbumTf, "[%Album%]")                                                    \
    X(mbidTitleFormat, setMbidTitleFormat, kMbidTf, "[%Musicbrainz_Trackid%]")                                         \
    X(excludedArtistsPatternList, setExcludedArtistsPatternList, kExcludeArtists, "")                                  \
    X(excludedTitlesPatternList, setExcludedTitlesPatternList, kExcludeTitles, "")                                     \
    X(excludedAlbumsPatternList, setExcludedAlbumsPatternList, kExcludeAlbums, "")                                     \
    X(excludedTitleFormatExpression, setExcludedTitleFormatExpression, kExcludeTf, "")                                 \
    X(excludedGenreTemplateValueList, setExcludedGenreTemplateValueList, kExcludeTemplateGenre, "Podcast")             \
    X(excludedMediaKindTemplateValueList, setExcludedMediaKindTemplateValueList, kExcludeTemplateMediaKind,            \
      "Audiobook")                                                                                                     \
    X(excludedPathTemplateValueList, setExcludedPathTemplateValueList, kExcludeTemplatePath, ".mpc; /other/")          \
    X(excludedCommentTemplateValueList, setExcludedCommentTemplateValueList, kExcludeTemplateComment, "Interview")

#define DEFINE_STRING_SETTING(getter, setter, key, defaultValue)                                                       \
    std::string getter()                                                                                               \
    {                                                                                                                  \
        return getString(key, defaultValue);                                                                           \
    }                                                                                                                  \
                                                                                                                       \
    void setter(const std::string& value)                                                                              \
    {                                                                                                                  \
        setString(key, value);                                                                                         \
    }

LASTFM_STRING_SETTINGS(DEFINE_STRING_SETTING)

#undef DEFINE_STRING_SETTING
#undef LASTFM_STRING_SETTINGS

bool treatVariousArtistsAsEmpty()
{
    return getBool(kTreatVariousArtistsAsEmpty, false);
}

void setTreatVariousArtistsAsEmpty(bool enabled)
{
    setBool(kTreatVariousArtistsAsEmpty, enabled);
}

} // namespace lastfm::settings
