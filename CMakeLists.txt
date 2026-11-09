# doa4retail - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(doa4retail LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the vendored ReXGlue SDK (git submodule, see thirdparty/NOTICE.md)
# when present; an explicit -DREXSDK_DIR still overrides it.
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to rexglue-sdk source tree (vendored submodule)")
endif()

include(generated/rexglue.cmake)

# Sources
set(DOA4RETAIL_SOURCES
    src/main.cpp
    src/doa4_preview_cut.cpp
    src/doa4_widescreen.cpp
    src/doa4_skip_intro.cpp
    src/doa4_attract_fade.cpp
    src/doa4_frame_probe.cpp
)

if(WIN32)
    add_executable(doa4retail WIN32 ${DOA4RETAIL_SOURCES})
else()
    add_executable(doa4retail ${DOA4RETAIL_SOURCES})
endif()

rexglue_setup_target(doa4retail GPU_PLUGINS xenos)

# Lean play profile: compiles out all project-side instrumentation (the F5/F6
# debug tab, the state poller / [state] label, the DOA4_DUMP_IMAGE hook) so
# play sessions carry zero debug overhead. Pair with REX_DIAG=0 to silence
# the SDK's TEMP probes at runtime (see start_doa4_lean.ps1). Dev builds keep
# the default OFF. Build with: --preset win-amd64-relwithdebinfo-lean
option(DOA4_LEAN "Lean play build: no debug tab / state poller / dump hook" OFF)
if(DOA4_LEAN)
    target_compile_definitions(doa4retail PRIVATE DOA4_LEAN=1)
endif()
