#include <glob_selector/glob_selector.hpp>

#include <logging/logging.hpp>

#include <regex>
#include <string>

namespace AssetPackerLib
{
namespace glob_selector
{
namespace
{
    // Convert a glob-like wildcard pattern into a safe ECMAScript regex string.
    std::string patternToRegex(const std::string& pattern)
    {
        const std::string metaChars = ".^$+(){}|[]\\";
        std::string result;
        result.reserve(pattern.size() * 2 + 2);
        result.push_back('^');
        for (const char ch : pattern)
        {
            switch (ch)
            {
            case '*':
                result.append(".*");
                break;
            case '?':
                result.push_back('.');
                break;
            default:
                if (metaChars.find(ch) != std::string::npos)
                {
                    result.push_back('\\');
                }
                result.push_back(ch);
                break;
            }
        }
        result.push_back('$');
        return result;
    }
} // namespace

bool hasGlobPattern(std::string_view text)
{
    return text.find_first_of("*?[]") != std::string_view::npos;
}

std::vector<std::filesystem::path> expandGlobPattern(const std::filesystem::path& pattern)
{
    std::vector<std::filesystem::path> matches;
    const auto directory = pattern.has_parent_path() ? pattern.parent_path() : std::filesystem::current_path();
    if (!std::filesystem::exists(directory))
    {
        LOGIFACE_LOG(warn, "Pattern directory does not exist: " + directory.string());
        return matches;
    }

    const auto namePattern = pattern.filename().string();
    // Translate the wildcard pattern into a case-insensitive ECMAScript regex for matching file names.
    const std::regex matcher(patternToRegex(namePattern), std::regex::ECMAScript | std::regex::icase);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (std::regex_match(entry.path().filename().string(), matcher))
        {
            matches.push_back(entry.path());
        }
    }

    return matches;
}

} // namespace glob_selector
} // namespace AssetPackerLib
