// see line ~455 for the related comments regarding the NOLINT.
// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange)
#include <gltf_loader/asset_loader.hpp>

#include <logging/logging.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <concepts>

namespace AssetPackerLib
{
namespace
{
    /// @brief Convert a fastgltf 4-component vector to a std::array.
    /// @param value The source vector values.
    constexpr std::array<float, 4> toArray(const fastgltf::math::fvec4& value)
    {
        return { value[0], value[1], value[2], value[3] };
    }

    /// @brief Convert a fastgltf 3-component vector to a std::array.
    /// @param value The source vector values.
    constexpr std::array<float, 3> toArray(const fastgltf::math::fvec3& value)
    {
        return { value[0], value[1], value[2] };
    }

    /// @brief Render an array of floats as JSON into the provided stream.
    /// @param stream The stream that receives the formatted floats.
    /// @param values Values that should be written as a comma separated list.
    template <std::size_t N>
    requires std::floating_point<typename std::array<float, N>::value_type>
    void appendArray(std::ostringstream& stream, const std::array<float, N>& values)
    {
        stream << '[';
        for (std::size_t i = 0; i < N; ++i)
        {
            if (i > 0)
            {
                stream << ',';
            }
            stream << values[i];
        }
        stream << ']';
    }

    /// @brief Find a named attribute in the provided primitive.
    /// @param primitive The primitive to inspect for attributes.
    /// @param name The attribute identifier to locate.
    const fastgltf::Attribute* findAttribute(const fastgltf::Primitive& primitive, std::string_view name)
    {
        for (const auto& attribute : primitive.attributes)
        {
            if (attribute.name == name)
            {
                return &attribute;
            }
        }
        return nullptr;
    }

    /// @brief Convert a fastgltf alpha mode into its string representation.
    /// @param mode The mode to translate.
    std::string alphaModeToString(fastgltf::AlphaMode mode)
    {
        switch (mode)
        {
        case fastgltf::AlphaMode::Blend:
            return "Blend";
        case fastgltf::AlphaMode::Mask:
            return "Mask";
        case fastgltf::AlphaMode::Opaque:
        default:
            return "Opaque";
        }
    }

    /// @brief Concept for any optional texture view that exposes a textureIndex.
    template <typename OptionalTexture>
    concept HasTextureIndex = requires(const OptionalTexture& info) {
        { info ? info->textureIndex : 0u } -> std::convertible_to<uint32_t>;
    };

    /// @brief Add a texture index to the target list when available.
    /// @param destination The texture list that should receive the index.
    /// @param info Optional texture metadata to evaluate.
    template <HasTextureIndex OptionalTexture>
    void appendTexture(std::vector<texture>& destination, const OptionalTexture& info)
    {
        if (!info)
        {
            return;
        }

        const auto index = info->textureIndex;
        if (index > std::numeric_limits<uint32_t>::max())
        {
            return;
        }

        destination.push_back(texture{ static_cast<uint32_t>(index) });
    }

    /// @brief Serialize the material's PBR properties into a compact JSON string.
    /// @param material The original fastgltf material to record.
    std::string encodeMaterialPBR(const fastgltf::Material& material)
    {
        std::ostringstream stream;
        stream << '{';
        bool first = true;

        auto maybeComma = [&]() {
            if (!first)
            {
                stream << ',';
            }
            first = false;
        };

        auto appendNumeric = [&](std::string_view key, float value) {
            maybeComma();
            stream << '"' << key << "\":" << value;
        };

        auto appendBoolean = [&](std::string_view key, bool value) {
            maybeComma();
            stream << '"' << key << "\":" << (value ? "true" : "false");
        };

        auto appendArrayField = [&](std::string_view key, const auto& values) {
            maybeComma();
            stream << '"' << key << "\":";
            appendArray(stream, values);
        };

        auto appendTextureIndexField = [&](std::string_view key, const auto& info) {
            if (!info)
            {
                return;
            }
            maybeComma();
            stream << '"' << key << "\":" << info->textureIndex;
        };

        appendArrayField("baseColorFactor", toArray(material.pbrData.baseColorFactor));
        appendNumeric("metallicFactor", material.pbrData.metallicFactor);
        appendNumeric("roughnessFactor", material.pbrData.roughnessFactor);
        appendArrayField("emissiveFactor", toArray(material.emissiveFactor));
        appendNumeric("emissiveStrength", material.emissiveStrength);
        appendNumeric("alphaCutoff", material.alphaCutoff);
        appendNumeric("ior", material.ior);
        appendNumeric("dispersion", material.dispersion);
        appendBoolean("doubleSided", material.doubleSided);
        appendBoolean("unlit", material.unlit);
        maybeComma();
        stream << '"' << "alphaMode" << "\":" << '"' << alphaModeToString(material.alphaMode) << '"';

        appendTextureIndexField("baseColorTexture", material.pbrData.baseColorTexture);
        appendTextureIndexField("metallicRoughnessTexture", material.pbrData.metallicRoughnessTexture);
        appendTextureIndexField("normalTexture", material.normalTexture);
        appendTextureIndexField("occlusionTexture", material.occlusionTexture);
        appendTextureIndexField("emissiveTexture", material.emissiveTexture);
        appendTextureIndexField("packedNormalRoughnessMetallicTexture", material.packedNormalMetallicRoughnessTexture);

        if (material.sheen)
        {
            appendArrayField("sheenColorFactor", toArray(material.sheen->sheenColorFactor));
            appendNumeric("sheenRoughnessFactor", material.sheen->sheenRoughnessFactor);
            appendTextureIndexField("sheenColorTexture", material.sheen->sheenColorTexture);
            appendTextureIndexField("sheenRoughnessTexture", material.sheen->sheenRoughnessTexture);
        }

        if (material.clearcoat)
        {
            appendNumeric("clearcoatFactor", material.clearcoat->clearcoatFactor);
            appendNumeric("clearcoatRoughnessFactor", material.clearcoat->clearcoatRoughnessFactor);
            appendTextureIndexField("clearcoatTexture", material.clearcoat->clearcoatTexture);
            appendTextureIndexField("clearcoatRoughnessTexture", material.clearcoat->clearcoatRoughnessTexture);
            appendTextureIndexField("clearcoatNormalTexture", material.clearcoat->clearcoatNormalTexture);
        }

        if (material.transmission)
        {
            appendNumeric("transmissionFactor", material.transmission->transmissionFactor);
            appendTextureIndexField("transmissionTexture", material.transmission->transmissionTexture);
        }

        if (material.volume && material.volume->thicknessTexture)
        {
            appendNumeric("thicknessFactor", material.volume->thicknessFactor);
            appendTextureIndexField("thicknessTexture", material.volume->thicknessTexture);
            appendNumeric("attenuationDistance", material.volume->attenuationDistance);
            appendArrayField("attenuationColor", toArray(material.volume->attenuationColor));
        }

        if (material.diffuseTransmission)
        {
            appendNumeric("diffuseTransmissionFactor", material.diffuseTransmission->diffuseTransmissionFactor);
            appendTextureIndexField("diffuseTransmissionTexture", material.diffuseTransmission->diffuseTransmissionTexture);
        }

        if (material.specular)
        {
            appendNumeric("specularFactor", material.specular->specularFactor);
            appendTextureIndexField("specularTexture", material.specular->specularTexture);
            appendArrayField("specularColorFactor", toArray(material.specular->specularColorFactor));
            appendTextureIndexField("specularColorTexture", material.specular->specularColorTexture);
        }

        stream << '}';
        return stream.str();
    }

    /// @brief Load vec3 data from an accessor into a typed buffer.
    /// @param asset The owning glTF asset used for accessors.
    /// @param accessor The accessor that points to vec3 data.
    std::vector<Vector3> readVector3(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor)
    {
        std::vector<Vector3> result;
        result.reserve(accessor.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset, accessor, [&](const fastgltf::math::fvec3& value) {
            result.push_back(Vector3{ value[0], value[1], value[2] });
        });
        return result;
    }

    /// @brief Load vec2 data from an accessor into a typed buffer.
    /// @param asset The owning glTF asset used for accessors.
    /// @param accessor The accessor that points to vec2 data.
    std::vector<Vector2> readVector2(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor)
    {
        std::vector<Vector2> result;
        result.reserve(accessor.count);
        fastgltf::iterateAccessor<fastgltf::math::fvec2>(asset, accessor, [&](const fastgltf::math::fvec2& value) {
            result.push_back(Vector2{ value[0], value[1] });
        });
        return result;
    }

    /// @brief Extract index data from an accessor into a contiguous vector.
    /// @param asset The glTF asset that contains the accessor.
    /// @param accessor The accessor that references index data.
    std::vector<uint32_t> readIndices(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor)
    {
        std::vector<uint32_t> result;
        result.reserve(accessor.count);
        fastgltf::iterateAccessor<uint32_t>(asset, accessor, [&](uint32_t value) {
            result.push_back(value);
        });
        return result;
    }

    /// @brief Build a material entry from a glTF material and append it to the offer.
    /// @param offer The accumulating offer data structure.
    /// @param gltfMaterial The glTF material source.
    /// @param index The material index used for naming fallbacks.
    void appendMaterial(AssetOffer& offer, const fastgltf::Material& gltfMaterial, std::size_t index)
    {
        material entry;
        entry.shader_type = static_cast<ShaderType>(offer.materials.size());
        const std::string generated = "material_" + std::to_string(index);
        const std::string name = gltfMaterial.name.empty() ? generated : std::string(gltfMaterial.name);
        entry.name = name;

        std::vector<texture> textures;
        appendTexture(textures, gltfMaterial.pbrData.baseColorTexture);
        appendTexture(textures, gltfMaterial.pbrData.metallicRoughnessTexture);
        appendTexture(textures, gltfMaterial.normalTexture);
        appendTexture(textures, gltfMaterial.occlusionTexture);
        appendTexture(textures, gltfMaterial.emissiveTexture);
        appendTexture(textures, gltfMaterial.packedNormalMetallicRoughnessTexture);
        if (gltfMaterial.sheen)
        {
            appendTexture(textures, gltfMaterial.sheen->sheenColorTexture);
            appendTexture(textures, gltfMaterial.sheen->sheenRoughnessTexture);
        }
        if (gltfMaterial.clearcoat)
        {
            appendTexture(textures, gltfMaterial.clearcoat->clearcoatTexture);
            appendTexture(textures, gltfMaterial.clearcoat->clearcoatRoughnessTexture);
            appendTexture(textures, gltfMaterial.clearcoat->clearcoatNormalTexture);
        }
        if (gltfMaterial.specular)
        {
            appendTexture(textures, gltfMaterial.specular->specularTexture);
            appendTexture(textures, gltfMaterial.specular->specularColorTexture);
        }
        if (gltfMaterial.transmission)
        {
            appendTexture(textures, gltfMaterial.transmission->transmissionTexture);
        }
        if (gltfMaterial.volume && gltfMaterial.volume->thicknessTexture)
        {
            appendTexture(textures, gltfMaterial.volume->thicknessTexture);
        }
        if (gltfMaterial.diffuseTransmission)
        {
            appendTexture(textures, gltfMaterial.diffuseTransmission->diffuseTransmissionTexture);
        }

        entry.textures = std::move(textures);
        const auto payload = encodeMaterialPBR(gltfMaterial);
        entry.material_data.assign(payload.begin(), payload.end());

        offer.materials.push_back(std::move(entry));
        offer.shader_names.push_back(name);
    }

    /// @brief Populate the offer with every material defined in the asset.
    /// @param offer The accumulating offer data structure.
    /// @param asset The parsed glTF asset containing materials.
    void buildMaterials(AssetOffer& offer, const fastgltf::Asset& asset)
    {
        if (asset.materials.empty())
        {
            fastgltf::Material fallback;
            appendMaterial(offer, fallback, 0);
            offer.shader_names.back() = "default_material";
            return;
        }

        for (std::size_t index = 0; index < asset.materials.size(); ++index)
        {
            appendMaterial(offer, asset.materials[index], index);
        }
    }

    /// @brief Collect mesh primitives and their materials into offer meshes.
    /// @param offer The accumulating offer data structure.
    /// @param asset The parsed glTF asset containing meshes.
    void buildMeshes(AssetOffer& offer, const fastgltf::Asset& asset)
    {
        if (offer.materials.empty())
        {
            return;
        }

        for (const auto& gltfMesh : asset.meshes)
        {
            for (const auto& primitive : gltfMesh.primitives)
            {
                const auto* positionAttribute = findAttribute(primitive, "POSITION");
                if (!positionAttribute)
                {
                    continue;
                }

                const auto& positionAccessor = asset.accessors[positionAttribute->accessorIndex];
                const auto positions = readVector3(asset, positionAccessor);
                if (positions.empty())
                {
                    continue;
                }

                auto normals = std::vector<Vector3>(positions.size(), Vector3{ 0.0f, 0.0f, 0.0f });
                if (const auto* normalAttribute = findAttribute(primitive, "NORMAL"))
                {
                    const auto read = readVector3(asset, asset.accessors[normalAttribute->accessorIndex]);
                    if (read.size() == positions.size())
                    {
                        normals = read;
                    }
                }

                auto uvs = std::vector<Vector2>(positions.size(), Vector2{ 0.0f, 0.0f });
                if (const auto* uvAttribute = findAttribute(primitive, "TEXCOORD_0"))
                {
                    const auto read = readVector2(asset, asset.accessors[uvAttribute->accessorIndex]);
                    if (read.size() == positions.size())
                    {
                        uvs = read;
                    }
                }

                if (!primitive.indicesAccessor)
                {
                    continue;
                }

                const auto indices = readIndices(asset, asset.accessors[*primitive.indicesAccessor]);
                if (indices.empty())
                {
                    continue;
                }

                mesh meshData;
                meshData.vertices = positions;
                meshData.normals = std::move(normals);
                meshData.uvs = std::move(uvs);
                meshData.indices = indices;

                if (!meshData.validate())
                {
                    continue;
                }

                subMesh chunk;
                chunk.index_start = 0;
                chunk.index_count = static_cast<uint32_t>(meshData.indices.size());
                chunk.material_index = static_cast<MaterialIndexType>(primitive.materialIndex ? *primitive.materialIndex : 0);
                if (chunk.material_index >= offer.materials.size())
                {
                    chunk.material_index = 0;
                }

                meshData.subMeshes.push_back(chunk);
                offer.meshes.push_back(std::move(meshData));
            }
        }
    }
} // namespace

/// @brief Load a glTF asset from disk and convert it into an AssetOffer.
/// @param path Path to the glTF file.
AssetLoadResult loadGltfAssetFromFile(const std::filesystem::path& path)
{
    if (path.empty())
    {
        LOGIFACE_LOG(error, "Input path is empty");
        return AssetLoadResult::failure("Input path is empty");
    }

    LOGIFACE_LOG(info, "Loading glTF asset: " + path.string());

    if (!std::filesystem::exists(path))
    {
        LOGIFACE_LOG(error, "glTF file does not exist: " + path.string());
        return AssetLoadResult::failure("glTF file does not exist: " + path.string());
    }

    auto bufferResult = fastgltf::GltfDataBuffer::FromPath(path);
    if (!bufferResult)
    {
        const auto message = std::string(fastgltf::getErrorMessage(bufferResult.error()));
        LOGIFACE_LOG(error, "Failed to read glTF buffer: " + message);
        return AssetLoadResult::failure(message);
    }

    fastgltf::GltfDataBuffer buffer = std::move(bufferResult.get());
    fastgltf::Parser parser;
    
    // Suppress clang static analyzer EnumCastOutOfRange for the combined Options.
    // Reason: `fastgltf::Options` is an enum class used as bitflags. The analyzer
    // reports a false positive when individual flag values are OR'ed together and
    // the resulting integer doesn't match any single enumerator. The combination is
    // intentional and valid for the library's API; at runtime the underlying value
    // is within the enum's underlying integer type and accepted by `parser.loadGltf`.
    constexpr auto loadOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;

    auto assetResult = parser.loadGltf(buffer, path.parent_path(), loadOptions);
    if (!assetResult)
    {
        const auto message = std::string(fastgltf::getErrorMessage(assetResult.error()));
        LOGIFACE_LOG(error, "Failed to parse glTF asset: " + message);
        return AssetLoadResult::failure(message);
    }

    const auto& asset = assetResult.get();
    AssetOffer offer;
    buildMaterials(offer, asset);
    buildMeshes(offer, asset);

    LOGIFACE_LOG(info, "Loaded asset with " + std::to_string(offer.meshes.size()) + " meshes and " + std::to_string(offer.materials.size()) + " materials.");

    return AssetLoadResult::success(std::move(offer));
}

/// @brief Merge another offer into the combiner, remapping shader/material indices.
/// @param offer Asset offer to merge.
void AssetCombiner::add(AssetOffer&& offer)
{
    LOGIFACE_LOG(debug, "Combining asset with " + std::to_string(offer.meshes.size()) + " meshes and " + std::to_string(offer.materials.size()) + " materials.");
    const auto baseIndex = static_cast<MaterialIndexType>(materials_.size());

    for (auto& materialItem : offer.materials)
    {
        materialItem.shader_type = static_cast<ShaderType>(baseIndex + materialItem.shader_type);
    }

    for (auto& meshItem : offer.meshes)
    {
        for (auto& sub : meshItem.subMeshes)
        {
            sub.material_index = static_cast<MaterialIndexType>(baseIndex + sub.material_index);
        }
    }

    materials_.reserve(materials_.size() + offer.materials.size());
    shader_names_.reserve(shader_names_.size() + offer.shader_names.size());
    meshes_.reserve(meshes_.size() + offer.meshes.size());

    materials_.insert(materials_.end(), std::make_move_iterator(offer.materials.begin()), std::make_move_iterator(offer.materials.end()));
    shader_names_.insert(shader_names_.end(), std::make_move_iterator(offer.shader_names.begin()), std::make_move_iterator(offer.shader_names.end()));
    meshes_.insert(meshes_.end(), std::make_move_iterator(offer.meshes.begin()), std::make_move_iterator(offer.meshes.end()));
}

/// @brief Clear all accumulated meshes, materials, and shader names.
void AssetCombiner::reset() noexcept
{
    LOGIFACE_LOG(debug, "Resetting AssetCombiner to empty state");
    meshes_.clear();
    materials_.clear();
    shader_names_.clear();
}

/// @brief Access the merged mesh collection.
const std::vector<mesh>& AssetCombiner::meshes() const noexcept
{
    return meshes_;
}

/// @brief Access the merged material collection.
const std::vector<material>& AssetCombiner::materials() const noexcept
{
    return materials_;
}

/// @brief Access the merged shader name list.
const std::vector<std::string>& AssetCombiner::shaderNames() const noexcept
{
    return shader_names_;
}
} // namespace AssetPackerLib
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)