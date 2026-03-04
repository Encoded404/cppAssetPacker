#ifndef LIB_TYPES_HPP
#define LIB_TYPES_HPP

#include <array>
#include <vector>
#include <string>
#include <cstdint> // for uint*_t types

#include <bit> // for std::bit_cast
#include <algorithm> // for std::copy
#include <type_traits>

namespace AssetPackerLib
{
static_assert(std::is_trivially_copyable_v<float> && std::is_trivially_copyable_v<uint32_t> && std::is_trivially_copyable_v<uint16_t> && std::is_trivially_copyable_v<uint8_t>, "Types used with std::bit_cast must be trivially copyable");

    // type aliases to tune storage globally
    using IndexType = uint32_t;static_assert(__cplusplus >= 202002L, "C++20 or newer required for std::bit_cast");

    using BoneIndexType = uint16_t;
    using MaterialIndexType = uint16_t;
    using WeightByte = uint8_t;      // 0..255 => 0.0..1.0
    using ShaderType = uint16_t;     // for example, to select different shader variants based on material properties

    struct texture
    {
        uint32_t id;

        bool operator==(const texture& other) const
        {
            return id == other.id;
        }

        std::array<uint8_t, 4> toByteData() const
        {
            return std::bit_cast<std::array<uint8_t, 4>>(id);
        }
    };

    struct Vector3
    {
        float x, y, z;

        bool operator==(const Vector3& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }

        std::array<uint8_t, /*3 floats each with 4 bytes*/ 12> toByteData() const
        {
            std::array<uint8_t, 12> bytes;

            auto xb = std::bit_cast<std::array<uint8_t, 4>>(x);
            auto yb = std::bit_cast<std::array<uint8_t, 4>>(y);
            auto zb = std::bit_cast<std::array<uint8_t, 4>>(z);

            // Copy into the final array
            std::copy(xb.begin(), xb.end(), bytes.begin());
            std::copy(yb.begin(), yb.end(), bytes.begin() + 4);
            std::copy(zb.begin(), zb.end(), bytes.begin() + 8);

            return bytes;
        }
    };

    struct Vector2
    {
        float u, v;

        bool operator==(const Vector2& other) const
        {
            return u == other.u && v == other.v;
        }

        std::array<uint8_t, /*2 floats each with 4 bytes*/ 8> toByteData() const
        {
            std::array<uint8_t, 8> bytes;

            auto ub = std::bit_cast<std::array<uint8_t, 4>>(u);
            auto vb = std::bit_cast<std::array<uint8_t, 4>>(v);

            // Copy into the final array
            std::copy(ub.begin(), ub.end(), bytes.begin());
            std::copy(vb.begin(), vb.end(), bytes.begin() + 4);

            return bytes;
        }
    };

    struct material
    {
        ShaderType shader_type; // (uint16_t)
        std::string name;
        std::vector<texture> textures;
        std::vector<uint8_t> material_data;

        bool operator==(const material& other) const
        {
            return shader_type == other.shader_type &&
                   textures == other.textures &&
                   material_data == other.material_data &&
                   name == other.name;
        }

        // we dont include name in byte data since it's only used for shader selection and debugging, not for GPU consumption
        std::vector<uint8_t> toByteData() const
        {
            std::vector<uint8_t> bytes;
            // Serialized sizes: ShaderType (2 bytes) + texture count (uint16_t) + each texture (4 bytes) + material data length (uint16_t) + material data blob
            bytes.reserve(sizeof(ShaderType) + /*texture count*/ sizeof(uint16_t) + textures.size() * 4u + /*material data length*/ sizeof(uint16_t) + material_data.size());

            auto shaderTypeBytes = std::bit_cast<std::array<uint8_t, sizeof(ShaderType)>>(shader_type);
            bytes.insert(bytes.end(), shaderTypeBytes.begin(), shaderTypeBytes.end());

            // texture vector length, a uint16_t is sufficient since we won't expect more than 65535 textures per asset pack
            std::array<uint8_t, 2> textureCountBytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(textures.size()));
            bytes.insert(bytes.end(), textureCountBytes.begin(), textureCountBytes.end());
            for (const auto& t : textures)
            {
                std::array<uint8_t, 4> textureBytes = t.toByteData();
                // actual texture index
                bytes.insert(bytes.end(), textureBytes.begin(), textureBytes.end());
            }
            
            // material data length, a uint16_t is sufficient since we won't expect more than 65535 bytes of material data per material
            std::array<uint8_t, 2> materialDataLengthBytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(material_data.size()));
            bytes.insert(bytes.end(), materialDataLengthBytes.begin(), materialDataLengthBytes.end());
            // actual material data blob (for example, encoded PBR parameters)
            bytes.insert(bytes.end(), material_data.begin(), material_data.end());

            return bytes;
        }
    };

    struct subMesh
    {
        uint32_t index_start; // start index in the mesh's index buffer
        uint32_t index_count; // number of indices for this submesh
        MaterialIndexType material_index; // index into the mesh's material array (uint16_t)

        std::array<uint8_t, /*2 uint32_t + 1 uint16_t*/ 10> toByteData() const
        {
            std::array<uint8_t, 10> bytes;

            auto indexStartBytes = std::bit_cast<std::array<uint8_t, 4>>(index_start);
            auto indexCountBytes = std::bit_cast<std::array<uint8_t, 4>>(index_count);
            auto materialIndexBytes = std::bit_cast<std::array<uint8_t, 2>>(material_index);

            // Copy into the final array
            std::copy(indexStartBytes.begin(), indexStartBytes.end(), bytes.begin());
            std::copy(indexCountBytes.begin(), indexCountBytes.end(), bytes.begin() + 4);
            std::copy(materialIndexBytes.begin(), materialIndexBytes.end(), bytes.begin() + 8);

            return bytes;
        }
    };

    // we use a deinterleaved format for vertex data, so all vertex attributes are stored in separate arrays
    // this is better for direct upload to GPU buffers
    struct mesh
    {
        bool validate() const
        {
            if (vertices.size() != normals.size() || vertices.size() != uvs.size()) return false;
            if (indices.size() % 3u != 0u) return false;
            return true;
        }

        // per vertex data
        std::vector<Vector3> vertices;
        std::vector<Vector3> normals;
        std::vector<Vector2> uvs;
        
        std::vector<uint32_t> indices;
        
        std::vector<subMesh> subMeshes;

        std::vector<uint8_t> GetRawMeshData() const
        {
            std::vector<uint8_t> rawData;

            // we can calculate the total size needed for the raw data to reserve capacity upfront
            size_t totalSize = 0;
            totalSize += sizeof(uint32_t); // vertex count
            totalSize += vertices.size() * 12u; // vertex positions (3 floats = 12 bytes)
            totalSize += normals.size() * 12u; // vertex normals (3 floats = 12 bytes)
            totalSize += uvs.size() * 8u; // vertex UVs (2 floats = 8 bytes)

            totalSize += sizeof(uint32_t); // index count
            totalSize += indices.size() * 4u; // indices (uint32_t = 4 bytes)

            totalSize += sizeof(uint32_t); // submesh count
            totalSize += subMeshes.size() * 10u; // submesh serialized size = 4 + 4 + 2 = 10 bytes

            // reserve the total size to avoid multiple reallocations during insertion
            rawData.reserve(totalSize);

            // vertex count
            auto vertexCountBytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(vertices.size()));
            rawData.insert(rawData.end(), vertexCountBytes.begin(), vertexCountBytes.end());

            // vertex positions
            for (const auto& v : vertices)
            {
                auto vertexBytes = v.toByteData();
                rawData.insert(rawData.end(), vertexBytes.begin(), vertexBytes.end());
            }

            // vertex normals
            for (const auto& n : normals)
            {
                auto normalBytes = n.toByteData();
                rawData.insert(rawData.end(), normalBytes.begin(), normalBytes.end());
            }

            // vertex UVs
            for (const auto& uv : uvs)
            {
                auto uvBytes = uv.toByteData();
                rawData.insert(rawData.end(), uvBytes.begin(), uvBytes.end());
            }

            // index count
            auto indexCountBytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(indices.size()));
            rawData.insert(rawData.end(), indexCountBytes.begin(), indexCountBytes.end());

            // indices
            for (const auto& i : indices)
            {
                auto indexBytes = std::bit_cast<std::array<uint8_t, 4>>(i);
                rawData.insert(rawData.end(), indexBytes.begin(), indexBytes.end());
            }

            // submesh count
            auto subMeshCountBytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(subMeshes.size()));
            rawData.insert(rawData.end(), subMeshCountBytes.begin(), subMeshCountBytes.end());

            // submesh data
            for (const auto& sm : subMeshes)
            {
                auto subMeshBytes = sm.toByteData();
                rawData.insert(rawData.end(), subMeshBytes.begin(), subMeshBytes.end());
            }

            return rawData;
        }
    };
    
    struct boneWeight
    {
        std::array<BoneIndexType, 4> bone_indices; // up to 4 influencing bones (uint16_t each)
        std::array<WeightByte, 4> weights; // 0-255 representing 0.0 to 1.0 (uint8_t each)

        std::array<uint8_t, /*4 uint16_t + 4 uint8_t*/ 12> toByteData() const
        {
            std::array<uint8_t, 12> bytes;

            for (size_t i = 0; i < 4; ++i)
            {
                std::array<uint8_t, 2> boneIndexBytes = std::bit_cast<std::array<uint8_t, 2>>(bone_indices[i]);
                uint8_t weightByte = weights[i];

                // Copy into the final array
                std::copy(boneIndexBytes.begin(), boneIndexBytes.end(), bytes.begin() + i * 3); // each bone index takes 2 bytes
                bytes[i * 3 + 2] = weightByte; // each weight takes 1 byte, placed after the corresponding bone index
            }

            return bytes;
        }
    };
    
    struct skinnedMesh : public mesh
    {
        std::vector<boneWeight> bone_weights;

        std::vector<uint8_t> GetRawMeshData() const
        {
            std::vector<uint8_t> rawData = mesh::GetRawMeshData();

            // we can calculate the total size needed for the bone weight data to reserve capacity upfront
            size_t boneWeightDataSize = sizeof(uint32_t); // bone weight count
            // serialized boneWeight is 12 bytes: 4 * uint16_t (2 bytes each) + 4 * uint8_t
            boneWeightDataSize += bone_weights.size() * 12u; // bone weight data

            // reserve additional space for bone weight data
            rawData.reserve(rawData.size() + boneWeightDataSize);

            // bone weight count
            auto boneWeightCountBytes = std::bit_cast<std::array<uint8_t, 4>>(static_cast<uint32_t>(bone_weights.size()));
            rawData.insert(rawData.end(), boneWeightCountBytes.begin(), boneWeightCountBytes.end());

            // bone weight data
            for (const auto& bw : bone_weights)
            {
                auto boneWeightBytes = bw.toByteData();
                rawData.insert(rawData.end(), boneWeightBytes.begin(), boneWeightBytes.end());
            }

            return rawData;
        }
    };

} // namespace AssetPackerLib

#endif // LIB_TYPES_HPP