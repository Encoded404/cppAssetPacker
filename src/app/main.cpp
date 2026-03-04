#include <algorithm> // for std::copy and std::transform
#include <cctype>
#include <filesystem>

#include <iostream>
#include <fstream>

#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

#include <cstdint> // for uint*_t types
#include <bit> // for std::bit_cast
#include <type_traits> // for std::is_trivially_copyable_v

#include <gltf_loader/asset_loader.hpp>
#include <glob_selector/glob_selector.hpp>
#include <logging/logging.hpp>
#include <logging/ConsoleLogger.hpp>
#include <CLI/CLI.hpp>

const std::array<uint8_t, 3> FILE_MAGIC_START = { 0x1B, 0xEF, 0xF8 }; // decided by 8 coin flips each, heads 0, tails 1: ( 00011011, 11101111, 11111000)

typedef std::basic_ofstream<uint8_t> BinaryOutputStream;

namespace
{
static_assert(std::is_trivially_copyable_v<float> && std::is_trivially_copyable_v<uint32_t> && std::is_trivially_copyable_v<uint16_t> && std::is_trivially_copyable_v<uint8_t>, "Types used with std::bit_cast must be trivially copyable");

    std::string toLower(std::string_view text)
    {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lowered;
    }

    bool isModelFile(const std::filesystem::path& path)
    {
        const auto extension = toLower(path.extension().string());
        return extension == ".gltf" || extension == ".glb" || extension == ".glf";
    }

    std::vector<std::filesystem::path> resolveModelInputs(const std::vector<std::string>& specs)
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
                    if (isModelFile(candidate))
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

            if (!isModelFile(path))
            {
                LOGIFACE_LOG(warn, "Unsupported model extension: " + spec);
                continue;
            }

            resolved.push_back(path);
        }
        return resolved;
    }

    AssetPackerLib::AssetOffer buildModelPack(const std::vector<std::filesystem::path>& files)
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
        

    BinaryOutputStream& GetBinaryOutputStream(const std::filesystem::path& path)
    {
        static BinaryOutputStream stream;
        if (!stream.is_open())
        {
            stream.open(path, std::ios::binary);
        }

        stream.seekp(0);

        stream.write(FILE_MAGIC_START.data(), FILE_MAGIC_START.size());

        return stream;
    }

    void SaveModelPack(const AssetPackerLib::AssetOffer& pack, const std::filesystem::path& outputPath)
    {
        // log general info about the pack contents
        LOGIFACE_LOG(info, "Model pack contains " + std::to_string(pack.meshes.size()) + " meshes, " + std::to_string(pack.materials.size()) + " materials, and " + std::to_string(pack.shader_names.size()) + " shader names.");
        // log vertex, index and UV counts for each mesh
        for (size_t i = 0; i < pack.meshes.size(); ++i)
        {
            const auto& mesh = pack.meshes[i];
            LOGIFACE_LOG(info, "Mesh " + std::to_string(i) + ": " + std::to_string(mesh.vertices.size()) + " vertices, " + std::to_string(mesh.indices.size()) + " indices, " + std::to_string(mesh.uvs.size()) + " UVs, " + std::to_string(mesh.subMeshes.size()) + " submeshes.");
        }

        BinaryOutputStream& stream = GetBinaryOutputStream(outputPath);
        if (!stream)
        {
            LOGIFACE_LOG(error, "Failed to open output file: " + outputPath.string());
            return;
        }

        // write material count, a uint16_t is sufficient since we won't expect more than 65535 materials per pack
        auto materialCountBytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(pack.materials.size()));
        stream.write(materialCountBytes.data(), materialCountBytes.size());

        // write each material's serialized byte data
        for (const auto& material : pack.materials)
        {
            const auto materialBytes = material.toByteData();
            stream.write(materialBytes.data(), materialBytes.size());
        }

        // write mesh count, a uint16_t is sufficient since we won't expect more than 65535 meshes per pack
        auto meshCountBytes = std::bit_cast<std::array<uint8_t, 2>>(static_cast<uint16_t>(pack.meshes.size()));
        stream.write(meshCountBytes.data(), meshCountBytes.size());

        // write each mesh's serialized byte data
        for (const auto& mesh : pack.meshes)
        {
            const auto meshBytes = mesh.GetRawMeshData();
            stream.write(meshBytes.data(), meshBytes.size());
        }
    }
} // namespace


static std::shared_ptr<Logiface::ConsoleLogger> app_logger;

// forward declarations
void initializeLogger();

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    initializeLogger();

    LOGIFACE_LOG(info, "Modern CMake Template CLI started");

    // Use the public library API (class with two example functions)
    // modern_cmake_template::example api;

    if (!args.empty()) {
        std::string args_list;
        args_list = "Args:";
        for (auto a : args) args_list += " \"" + a + '"';
        LOGIFACE_LOG(debug, args_list);
    }

    CLI::App app{"Modern CMake Template CLI"};

    std::vector<std::string> files;
    bool verbose = false;
    bool imageMode = false;
    bool modelMode = false;

    app.add_flag("-v,--verbose", verbose, "Enable verbose logging");

    auto mode_group = app.add_option_group("Source Mode", "Select the type of processing to perform on the input files");
    auto image_flag = mode_group->add_flag("-i,--image", imageMode, "Enable image processing (currently no-op)");
    auto model_flag = mode_group->add_flag("-m,--models", modelMode, "Enable model packing via provided files");
    mode_group->require_option(1); // require exactly one mode to be selected

    // this ensures they are mutually exclusive
    model_flag->excludes(image_flag);            // add first exclusion
    image_flag->excludes(model_flag);            // add second exclusion

    app.add_option("files", files, "Input files")
    ->check(CLI::ExistingFile);

    bool EnsureOutputDirectoryExists = true;
    app.add_option("-p,--parents", EnsureOutputDirectoryExists, "Automatically create parent directories for the output file if they do not exist");
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
        if (isatty(fileno(stdin)))
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

    if(EnsureOutputDirectoryExists)
    {
        const auto outputPath = std::filesystem::path(output_file);
        const auto parentDir = outputPath.parent_path();
        if (!EnsureDirectoryExists(parentDir))
        {
            LOGIFACE_LOG(error, "Failed to ensure output directory exists: " + parentDir.string());
            return 1;
        }
    }

    if (imageMode)
    {
        LOGIFACE_LOG(info, "Image processing requested but not implemented yet.");
    }
    else if (modelMode)
    {
        if (files.empty())
        {
            LOGIFACE_LOG(error, "No files provided for model packing.");
            return 1;
        }

        const auto resolvedModelFiles = resolveModelInputs(files);
        if (resolvedModelFiles.empty())
        {
            LOGIFACE_LOG(error, "No model files matched the provided inputs.");
            return 1;
        }

        LOGIFACE_LOG(info, "Packing " + std::to_string(resolvedModelFiles.size()) + " model file(s).");
        const auto pack = buildModelPack(resolvedModelFiles);
        SaveModelPack(pack, output_file);
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

void initializeLogger() {
    app_logger = std::make_shared<Logiface::ConsoleLogger>();
    Logiface::SetLogger(app_logger);
}
