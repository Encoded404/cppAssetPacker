add_library(${GLOB_SELECTOR_TARGET} STATIC)
target_compile_features(${GLOB_SELECTOR_TARGET} PUBLIC cxx_std_20)
target_sources(${GLOB_SELECTOR_TARGET} PRIVATE glob_selector.cpp)

target_link_libraries(${GLOB_SELECTOR_TARGET} PUBLIC logiface)

target_compile_options(${GLOB_SELECTOR_TARGET} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Werror>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

if(CMAKE_CONFIGURATION_TYPES)
    target_compile_options(${GLOB_SELECTOR_TARGET} PRIVATE
        "$<$<CONFIG:Debug>:-g>"
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Release>:-O3>"
    )
endif()

# Include directories
target_include_directories(${GLOB_SELECTOR_TARGET}
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/include>
)
