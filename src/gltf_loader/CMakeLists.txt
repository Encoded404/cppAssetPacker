# Set the C++ standard and enable compiler features
add_library(${GLTF_LOADER_TARGET} STATIC)
target_compile_features(${GLTF_LOADER_TARGET} PUBLIC cxx_std_20)

target_link_libraries(${GLTF_LOADER_TARGET} PUBLIC logiface)
target_link_libraries(${GLTF_LOADER_TARGET} PRIVATE fastgltf::fastgltf)

# Compiler-specific warning options for the library target
target_compile_options(${GLTF_LOADER_TARGET} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Werror>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Multi-configuration generator options
if(CMAKE_CONFIGURATION_TYPES)
    target_compile_options(${GLTF_LOADER_TARGET} PRIVATE
        "$<$<CONFIG:Debug>:-g>"
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Release>:-O3>"
    )
endif()

# Include directories
target_include_directories(${GLTF_LOADER_TARGET}
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/include>
)

# Source files
target_sources(${GLTF_LOADER_TARGET} PRIVATE
    asset_loader.cpp
)
