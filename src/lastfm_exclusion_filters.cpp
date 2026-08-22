//
//  lastfm_exclusion_filters.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_exclusion_filters.h"
#include "debug.h"
#include "lastfm_settings.h"

#include <pfc/SmartStrStr.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace
{
class TextOrRegexFilter
{
  public:
    explicit TextOrRegexFilter(const char* what) : what_(what)
    {
    }

    bool matches(const std::string& value, const std::string& rawRules)
    {
        rebuildIfNeeded(rawRules);
        std::lock_guard<std::mutex> lock(m_);
        if (raw_.empty())
            return false;

        const std::string vLower = searchKey(value);

        for (const auto& needle : substrLower_)
        {
            if (!needle.empty() && vLower.find(needle) != std::string::npos)
                return true;
        }

        for (const auto& rx : regexes_)
        {
            try
            {
                if (std::regex_search(value, rx.re))
                    return true;
            }
            catch (const std::regex_error&)
            {
            }
        }

        return false;
    }

    void logMatchLimited(const std::string& value)
    {
        int r = remaining_.load(std::memory_order_relaxed);
        while (r > 0)
        {
            if (remaining_.compare_exchange_weak(r, r - 1, std::memory_order_relaxed))
            {
                LFM_DEBUG("Excluded by " << what_ << " filter: " << value.c_str());
                return;
            }
        }
    }

  private:
    struct Rx
    {
        std::string pat;
        std::regex re;
    };

    static bool hasRegexMeta(const std::string& s)
    {
        for (char c : s)
        {
            switch (c)
            {
            case '.':
            case '^':
            case '$':
            case '|':
            case '?':
            case '*':
            case '+':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '\\':
                return true;
            default:
                break;
            }
        }
        return false;
    }

    static std::string trimCopy(const std::string& in)
    {
        std::size_t b = 0;
        while (b < in.size() && std::isspace((unsigned char)in[b]))
            ++b;
        std::size_t e = in.size();
        while (e > b && std::isspace((unsigned char)in[e - 1]))
            --e;
        return (e > b) ? in.substr(b, e - b) : std::string{};
    }

    static std::string searchKey(const std::string& in)
    {
        pfc::string8 folded = SmartStrStr::global().transformStr(in.c_str());
        std::string out;
        out.reserve(strlen(folded.c_str()));
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(folded.c_str()); *p; ++p)
            out.push_back((*p >= 'A' && *p <= 'Z') ? (char)(*p + ('a' - 'A')) : (char)*p);
        return out;
    }

    void rebuildIfNeeded(const std::string& rawRules)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (rawRules == raw_)
            return;

        raw_ = rawRules;
        substrLower_.clear();
        regexes_.clear();

        if (raw_.empty())
            return;

        constexpr std::size_t kMaxPatterns = 32;
        constexpr std::size_t kMaxLen = 256;

        std::size_t start = 0;
        while (start <= raw_.size() && (substrLower_.size() + regexes_.size()) < kMaxPatterns)
        {
            std::size_t end = raw_.find(';', start);
            if (end == std::string::npos)
                end = raw_.size();

            std::string entry = trimCopy(raw_.substr(start, end - start));
            start = end + 1;

            if (entry.empty())
                continue;

            if (entry.size() > kMaxLen)
                continue;

            if (!hasRegexMeta(entry))
            {
                substrLower_.push_back(searchKey(entry));
                continue;
            }

            try
            {
                regexes_.push_back(Rx{entry, std::regex(entry, std::regex::ECMAScript | std::regex::icase)});
            }
            catch (const std::regex_error&)
            {
                LFM_INFO("Exclude " << what_ << ": invalid regex ignored: " << entry.c_str());
            }
        }
    }

    const char* what_ = "";
    std::mutex m_;
    std::string raw_;
    std::vector<std::string> substrLower_;
    std::vector<Rx> regexes_;
    std::atomic<int> remaining_{10};
};

static TextOrRegexFilter g_excludeArtist("artist");
static TextOrRegexFilter g_excludeTitle("title");
static TextOrRegexFilter g_excludeAlbum("album");

static bool hasNonWhitespaceOutput(const char* value)
{
    if (!value)
        return false;

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    {
        if (!std::isspace(*p))
            return true;
    }

    return false;
}

class TitleFormattingFilter
{
  public:
    bool matches(const metadb_handle_ptr& track, const LastfmTrackInfo& evaluated, const file_info* externalInfo)
    {
        if (!track.is_valid())
            return false;

        service_ptr_t<titleformat_object> script;
        const std::string expr = lastfm::settings::excludedTitleFormatExpression();

        {
            std::lock_guard<std::mutex> lock(m_);
            rebuildIfNeededLocked(expr);
            script = script_;
        }

        if (!script.is_valid())
            return false;

        file_info_impl info;
        if (externalInfo)
            info.copy(*externalInfo);
        else if (!track->get_info(info))
            return false;

        // Exclusion TF sees the same core fields that Foo Scrobbler would submit,
        // after the configured input Title Formatting has already been evaluated.
        info.meta_set("ARTIST", evaluated.artist.c_str());
        info.meta_set("TITLE", evaluated.title.c_str());
        info.meta_set("ALBUM", evaluated.album.c_str());
        info.meta_set("ALBUM ARTIST", evaluated.albumArtist.c_str());
        if (const char* path = track->get_path())
            info.meta_set("FOO_SCROBBLER_PATH", path);

        pfc::string8 out;
        track->format_title_from_external_info(info, nullptr, out, script, nullptr);

        if (!hasNonWhitespaceOutput(out.c_str()))
            return false;

        return true;
    }

  private:
    void rebuildIfNeededLocked(const std::string& expr)
    {
        if (expr == raw_)
            return;

        raw_ = expr;
        script_.release();

        if (raw_.empty())
            return;

        static_api_ptr_t<titleformat_compiler> compiler;
        if (!compiler->compile(script_, raw_.c_str()))
            LFM_INFO("Exclude Title Formatting: invalid expression ignored.");
    }

    void logMatchLimited(const char* value)
    {
        int r = remaining_.load(std::memory_order_relaxed);
        while (r > 0)
        {
            if (remaining_.compare_exchange_weak(r, r - 1, std::memory_order_relaxed))
            {
                LFM_DEBUG("Excluded by Title Formatting filter: " << (value ? value : ""));
                return;
            }
        }
    }

    std::mutex m_;
    std::string raw_;
    service_ptr_t<titleformat_object> script_;
    std::atomic<int> remaining_{10};
};

static TitleFormattingFilter g_excludeTitleFormatting;

} // namespace

namespace lastfm
{
namespace exclusion_filters
{

bool isExcludedByTextOrRegexFilters(const std::string& artist, const std::string& title, const std::string& album)
{
    const std::string artistRules = lastfm::settings::excludedArtistsPatternList();
    if (!artistRules.empty() && g_excludeArtist.matches(artist, artistRules))
    {
        g_excludeArtist.logMatchLimited(artist);
        return true;
    }

    const std::string titleRules = lastfm::settings::excludedTitlesPatternList();
    if (!titleRules.empty() && g_excludeTitle.matches(title, titleRules))
    {
        g_excludeTitle.logMatchLimited(title);
        return true;
    }

    const std::string albumRules = lastfm::settings::excludedAlbumsPatternList();
    if (!album.empty() && !albumRules.empty() && g_excludeAlbum.matches(album, albumRules))
    {
        g_excludeAlbum.logMatchLimited(album);
        return true;
    }

    return false;
}

bool isExcludedByTitleFormattingFilter(const metadb_handle_ptr& track, const LastfmTrackInfo& evaluated,
                                       const file_info* externalInfo)
{
    return g_excludeTitleFormatting.matches(track, evaluated, externalInfo);
}

} // namespace exclusion_filters
} // namespace lastfm
