#ifndef ASSETPACKER_GLTF_LOADER_ASSET_LOADER_HPP
#define ASSETPACKER_GLTF_LOADER_ASSET_LOADER_HPP

#include "types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace AssetPackerLib
{
    struct AssetOffer
    {
        std::vector<mesh> meshes;
        std::vector<material> materials;
        std::vector<std::string> shader_names;
    };

    struct AssetLoadResult
    {
        AssetOffer offer;
        std::optional<std::string> error;

        [[nodiscard]] bool succeeded() const noexcept
        {
            return !error.has_value();
        }

        explicit operator bool() const noexcept
        {
            return succeeded();
        }

        static AssetLoadResult success(AssetOffer data)
        {
            AssetLoadResult result;
            result.offer = std::move(data);
            return result;
        }

        static AssetLoadResult failure(std::string message)
        {
            AssetLoadResult result;
            result.error = std::move(message);
            return result;
        }
    };

    AssetLoadResult loadGltfAssetFromFile(const std::filesystem::path& path);

    class AssetCombiner
    {
    public:
        void add(AssetOffer&& offer);
        void reset() noexcept;

        [[nodiscard]] const std::vector<mesh>& meshes() const noexcept;
        [[nodiscard]] const std::vector<material>& materials() const noexcept;
        [[nodiscard]] const std::vector<std::string>& shaderNames() const noexcept;

    private:
        std::vector<mesh> meshes_;
        std::vector<material> materials_;
        std::vector<std::string> shader_names_;
    };
} // namespace AssetPackerLib

#endif // ASSETPACKER_GLTF_LOADER_ASSET_LOADER_HPP
