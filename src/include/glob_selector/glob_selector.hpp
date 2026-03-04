#ifndef ASSETPACKER_GLOB_SELECTOR_HPP
#define ASSETPACKER_GLOB_SELECTOR_HPP

#include <filesystem>
#include <string_view>
#include <vector>

namespace AssetPackerLib
{
namespace glob_selector
{
    bool hasGlobPattern(std::string_view text);

    std::vector<std::filesystem::path> expandGlobPattern(const std::filesystem::path& pattern);
} // namespace glob_selector
} // namespace AssetPackerLib

#endif // ASSETPACKER_GLOB_SELECTOR_HPP
