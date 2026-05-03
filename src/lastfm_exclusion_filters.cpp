//
//  lastfm_exclusion_filters.cpp
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#include "lastfm_exclusion_filters.h"
#include "lastfm_prefs_pane.h"
#include "debug.h"

#include <atomic>
#include <cctype>
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

        const std::string vLower = lowerCopy(value);

        for (const auto& needle : substrLower_)
        {
            if (!needle.empty() && vLower.find(needle) != std::string::npos)
                return true;
        }

        for (const auto& rx : regexes_)
        {
            if (std::regex_search(value, rx.re))
                return true;
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

    static std::string lowerCopy(const std::string& in)
    {
        std::string out;
        out.reserve(in.size());
        for (unsigned char c : in)
            out.push_back((char)std::tolower(c));
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
                substrLower_.push_back(lowerCopy(entry));
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

} // namespace

namespace lastfm
{
namespace exclusion_filters
{

bool isExcludedByTextOrRegexFilters(const std::string& artist, const std::string& title, const std::string& album)
{
    const std::string artistRules = lastfmExcludedArtistsPatternList();
    if (!artistRules.empty() && g_excludeArtist.matches(artist, artistRules))
    {
        g_excludeArtist.logMatchLimited(artist);
        return true;
    }

    const std::string titleRules = lastfmExcludedTitlesPatternList();
    if (!titleRules.empty() && g_excludeTitle.matches(title, titleRules))
    {
        g_excludeTitle.logMatchLimited(title);
        return true;
    }

    const std::string albumRules = lastfmExcludedAlbumsPatternList();
    if (!album.empty() && !albumRules.empty() && g_excludeAlbum.matches(album, albumRules))
    {
        g_excludeAlbum.logMatchLimited(album);
        return true;
    }

    return false;
}

} // namespace exclusion_filters
} // namespace lastfm
