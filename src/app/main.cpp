#include <algorithm> // for std::copy and std::transform
#include <array>
#include <bit> // for std::bit_cast
#include <cctype>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>
#include <ios>
#include <iostream>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

#include <cstdint> // for uint*_t types
#include <type_traits> // for std::is_trivially_copyable_v

#include <gltf_loader/asset_loader.hpp>
#include <glob_selector/glob_selector.hpp>
#include <logging/logging.hpp>
#include <logging/ConsoleLogger.hpp>
#include <CLI/CLI.hpp>

const std::array<uint8_t, 3> FILE_MAGIC_START = { 0x1B, 0xEF, 0xF8 }; // decided by 8 coin flips each, heads 0, tails 1: ( 00011011, 11101111, 11111000)

using BinaryOutputStream = std::ofstream;

namespace
{
static_assert(std::is_trivially_copyable_v<float> && std::is_trivially_copyable_v<uint32_t> && std::is_trivially_copyable_v<uint16_t> && std::is_trivially_copyable_v<uint8_t>, "Types used with std::bit_cast must be trivially copyable");

    std::shared_ptr<Logiface::ConsoleLogger> app_logger;

    std::string ToLower(std::string_view text)
    {
        std::string lowered(text);
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lowered;
    }

    bool IsModelFile(const std::filesystem::path& path)
    {
        const auto extension = ToLower(path.extension().string());
        return extension == ".gltf" || extension == ".glb" || extension == ".glf";
    }

    std::vector<std::filesystem::path> ResolveModelInputs(const std::vector<std::string>& specs)
    {
        std::vector<std::filesystem::path> resolved;
        resolved.reserve(specs.size());
        for (const auto& spec : specs)
        {
            if (spec.empty())
            {
                continue;
            }

            const std::filesystem::path path(spec);
            if (AssetPackerLib::glob_selector::hasGlobPattern(spec))
            {
                const auto expanded = AssetPackerLib::glob_selector::expandGlobPattern(path);
                for (const auto& candidate : expanded)
                {
                    if (IsModelFile(candidate))
                    {
                        resolved.push_back(candidate);
                    }
                    else
                    {
                        LOGIFACE_LOG(warn, "Skipping non-model file from glob: " + candidate.string());
                    }
                }
                if (expanded.empty())
                {
                    LOGIFACE_LOG(warn, "Glob did not match any files: " + spec);
                }
                continue;
            }

            if (!IsModelFile(path))
            {
                LOGIFACE_LOG(warn, "Unsupported model extension: " + spec);
                continue;
            }

            resolved.push_back(path);
        }
        return resolved;
    }

    AssetPackerLib::AssetOffer BuildModelPack(const std::vector<std::filesystem::path>& files)
    {
        AssetPackerLib::AssetCombiner combiner;
        for (const auto& file : files)
        {
            AssetPackerLib::AssetLoadResult result = AssetPackerLib::loadGltfAssetFromFile(file);
            if (!result)
            {
                const auto message = result.error.value_or("unknown error");
                LOGIFACE_LOG(error, "Failed to load asset " + file.string() + ": " + message);
                continue;
            }
            combiner.add(std::move(result.offer));
        }

        AssetPackerLib::AssetOffer pack;
        pack.meshes = combiner.meshes();
        pack.materials = combiner.materials();
        pack.shader_names = combiner.shaderNames();
        return pack;
    }

    bool EnsureDirectoryExists(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return true;
        }

        if (!std::filesystem::exists(path))
        {
            try
            {
                std::filesystem::create_directories(path);
                return true;
            }
            catch(const std::exception& e)
            {
                LOGIFACE_LOG(error, "Failed to create directory: " + path.string() + ", error: " + e.what());
                return false;
            }
        }
        return true;
    }

    std::filesystem::path ResolveOutputPath(const std::filesystem::path& requested_output, const std::filesystem::path& first_input)
    {
        const bool output_is_directory =
            (!requested_output.empty() && requested_output.filename().empty()) ||
            std::filesystem::is_directory(requested_output);

        if (!output_is_directory)
        {
            return requested_output;
        }

        auto base_name = first_input.stem();
        if (base_name.empty())
        {
            base_name = first_input.filename();
        }
        if (base_name.empty())
        {
            base_name = "output";
        }

        const auto resolved_output = requested_output / (base_name.string() + ".bin");
        LOGIFACE_LOG(warn, "Output path is a directory; writing to " + resolved_output.string());
        return resolved_output;
    }

    std::optional<BinaryOutputStream> GetBinaryOutputStream(const std::filesystem::path& path)
    {
        BinaryOutputStream stream(path, std::ios::binary | std::ios::trunc);

        if(!stream.is_open())
        {
            LOGIFACE_LOG(error, "Failed to open output file: " + path.string() + ", error: " + std::strerror(errno));
            return std::nullopt;
        }

        LOGIFACE_LOG(debug, "Successfully opened output file: " + path.string());

        return stream;
    }

    bool WriteBytes(BinaryOutputStream& stream, const uint8_t* data, std::size_t size, std::string_view what)
    {
        stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!stream)
        {
            LOGIFACE_LOG(error, "Failed while writing " + std::string(what) + " to the output file");
            return false;
        }
        return true;
    }

    bool WriteBytes(BinaryOutputStream& stream, const std::vector<uint8_t>& data, std::string_view what)
    {
        return WriteBytes(stream, data.data(), data.size(), what);
    }

    bool WriteBytes(BinaryOutputStream& stream, const std::array<uint8_t, 2>& data, std::string_view what)
    {
        return WriteBytes(stream, data.data(), data.size(), what);
    }

    bool WriteBytes(BinaryOutputStream& stream, const std::array<uint8_t, 3>& data, std::string_view what)
    {
        return WriteBytes(stream, data.data(), data.size(), what);
    }

    bool SaveModelPack(const AssetPackerLib::AssetOffer& pack, const std::filesystem::path& output_path)
    {
        // log general info about the pack contents
        LOGIFACE_LOG(info, "Model pack contains " + std::to_string(pack.meshes.size()) + " meshes, " + std::to_string(pack.materials.size()) + " materials, and " + std::to_string(pack.shader_names.size()) + " shader names.");
        // log vertex, index and UV counts for each mesh
        for (size_t i = 0; i < pack.meshes.size(); ++i)
        {
            const auto& mesh = pack.meshes[i];
            LOGIFACE_LOG(info, "Mesh " + std::to_string(i) + ": " + std::to_string(mesh.vertices.size()) + " vertices, " + std::to_string(mesh.indices.size()) + " indices, " + std::to_string(mesh.uvs.size()) + " UVs, " + std::to_string(mesh.subMeshes.size()) + " submeshes.");
        }

        auto stream_opt = GetBinaryOutputStream(output_path);
        if (!stream_opt)
        {
            return false;
        }
        auto& stream = *stream_opt;

        if (!WriteBytes(stream, FILE_MAGIC_START, "file magic"))
        {
            return false;
        }

        // write material count, a uint16_t is sufficient since we won't expect more than 65535 materials per pack
        auto material_count_bytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(pack.materials.size()));
        if (!WriteBytes(stream, material_count_bytes, "material count"))
        {
            return false;
        }

        // write each material's serialized byte data
        for (const auto& material : pack.materials)
        {
            const auto material_bytes = material.toByteData();
            // write material size first as uint16_t
            auto material_size_bytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(material_bytes.size()));
            if (!WriteBytes(stream, material_size_bytes, "material size"))
            {
                return false;
            }
            if (!WriteBytes(stream, material_bytes, "material data"))
            {
                return false;
            }
        }

        // write mesh count, a uint16_t is sufficient since we won't expect more than 65535 meshes per pack
        auto mesh_count_bytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(pack.meshes.size()));
        if (!WriteBytes(stream, mesh_count_bytes, "mesh count"))
        {
            return false;
        }

        // write each mesh's serialized byte data
        for (const auto& mesh : pack.meshes)
        {
            const auto mesh_bytes = mesh.GetRawMeshData();
            if (!WriteBytes(stream, mesh_bytes, "mesh data"))
            {
                return false;
            }
        }

        stream.flush();
        if (!stream)
        {
            LOGIFACE_LOG(error, "Failed to flush output file: " + output_path.string());
            return false;
        }

        LOGIFACE_LOG(info, "Wrote model pack to " + output_path.string());
        return true;
    }

    void InitializeLogger()
    {
        app_logger = std::make_shared<Logiface::ConsoleLogger>();
        Logiface::SetLogger(app_logger);
    }
} // namespace

int main(int argc, char** argv) {
    try
    {
        const std::vector<std::string> args(argv + 1, argv + argc);

        InitializeLogger();

        LOGIFACE_LOG(info, "Modern CMake Template CLI started");

        // Use the public library API (class with two example functions)
        // modern_cmake_template::example api;

        if (!args.empty()) {
            std::string args_list = "Args:";
            for (const auto& a : args) {
                args_list += " \"" + a + '"';
            }
            LOGIFACE_LOG(debug, args_list);
        }

        CLI::App app{"Modern CMake Template CLI"};

        std::vector<std::string> files;
        bool verbose = false;
        bool image_mode = false;
        bool model_mode = false;

        app.add_flag("-v,--verbose", verbose, "Enable verbose logging");

        auto mode_group = app.add_option_group("Source Mode", "Select the type of processing to perform on the input files");
        auto image_flag = mode_group->add_flag("-i,--image", image_mode, "Enable image processing (currently no-op)");
        auto model_flag = mode_group->add_flag("-m,--models", model_mode, "Enable model packing via provided files");
        mode_group->require_option(1); // require exactly one mode to be selected

        // this ensures they are mutually exclusive
        model_flag->excludes(image_flag);            // add first exclusion
        image_flag->excludes(model_flag);            // add second exclusion

        app.add_option("files", files, "Input files")
        ->check(CLI::ExistingFile);

        bool ensure_output_directory_exists = false;
        app.add_flag("-p,--parents", ensure_output_directory_exists, "Automatically create parent directories for the output file if they do not exist");
        std::string output_file;
        app.add_option("-o,--output", output_file, "Output file path")
        ->required();

        CLI11_PARSE(app, argc, argv);

        if (verbose)
        {
            if (app_logger)
            {
                app_logger->SetLevel(Logiface::Level::debug);
            }
        }
        if (files.empty())
        {
            if (isatty(STDIN_FILENO))
            {
                LOGIFACE_LOG(error, "No input files provided. Use --help for usage information.");
                return 1;
            }

            std::string line;
            while (std::getline(std::cin, line))
            {
                if (!line.empty())
                {
                    files.push_back(line);
                }
            }
        }

        if (image_mode)
        {
            LOGIFACE_LOG(info, "Image processing requested but not implemented yet.");
        }
        else if (model_mode)
        {
            if (files.empty())
            {
                LOGIFACE_LOG(error, "No files provided for model packing.");
                return 1;
            }

            const auto resolved_model_files = ResolveModelInputs(files);
            if (resolved_model_files.empty())
            {
                LOGIFACE_LOG(error, "No model files matched the provided inputs.");
                return 1;
            }

            LOGIFACE_LOG(info, "Packing " + std::to_string(resolved_model_files.size()) + " model file(s).");
            const auto pack = BuildModelPack(resolved_model_files);
            const auto output_path = ResolveOutputPath(output_file, resolved_model_files.front());
            if (ensure_output_directory_exists)
            {
                const auto parent_dir = output_path.parent_path();
                if (!EnsureDirectoryExists(parent_dir))
                {
                    LOGIFACE_LOG(error, "Failed to ensure output directory exists: " + parent_dir.string());
                    return 1;
                }
            }

            if (!SaveModelPack(pack, output_path))
            {
                return 1;
            }
        }
        else
        {
            for (const auto& file : files) {
                LOGIFACE_LOG(info, "Processing file: " + file);
                // Here you would add your file processing logic
            }
        }

        LOGIFACE_LOG(info, "App completed");

        return 0;
    }
    catch (const std::exception& ex)
    {
        LOGIFACE_LOG(error, "Unhandled exception: " + std::string(ex.what()));
        return 1;
    }
    catch (...)
    {
        LOGIFACE_LOG(error, "Unhandled non-standard exception");
        return 1;
    }
}
