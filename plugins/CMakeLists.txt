# Optional upscale plugins for OpenTTD
# These are built as separate DLLs, distributed independently from the main game.

cmake_minimum_required(VERSION 3.16)

# FSR Reference Plugin (MIT licensed, can ship with the game)
add_library(fsr_plugin SHARED fsr_plugin/fsr_plugin.cpp)
target_include_directories(fsr_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
set_target_properties(fsr_plugin PROPERTIES
    OUTPUT_NAME "fsr_plugin"
    PREFIX ""
)
if(WIN32)
    target_link_libraries(fsr_plugin PRIVATE opengl32)
endif()

# DLSS Plugin Skeleton (for NVIDIA developers, NOT shipped with game)
# Uncomment to build -- requires Streamline SDK
# add_library(dlss_plugin SHARED dlss_plugin/dlss_plugin.cpp)
# target_include_directories(dlss_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/video)
# target_link_libraries(dlss_plugin PRIVATE sl.interposer)
