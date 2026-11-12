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

# The release zip vendors the FidelityFX SDK so the player's build does not have
# to git-clone ~700 MB during configure. CMake's own FetchContent override makes
# it use this copy and skip the download; without it, the SDK fetches as usual.
if(NOT FETCHCONTENT_SOURCE_DIR_FIDELITYFX
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fidelityfx/ffx-api/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_FIDELITYFX
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fidelityfx" CACHE PATH
        "Vendored FidelityFX SDK source tree")
    message(STATUS "Using the vendored FidelityFX SDK: ${FETCHCONTENT_SOURCE_DIR_FIDELITYFX}")
endif()

# The SDK's codegen emits generated/rexglue.cmake with an absolute path to the
# manifest. rexgluerd mis-decodes non-ASCII absolute paths (a folder such as
# "^'éç_(rfdr" arrives mangled) and then reports the misleading
# "not a manifest: no [project] section". Its codegen command already runs with
# WORKING_DIRECTORY = source dir, so rewrite that one argument to a relative
# path. Done on every configure because codegen rewrites the file each build.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    message(FATAL_ERROR "generated/rexglue.cmake is missing - run codegen first "
        "(compiler.exe does this, or: rexglue codegen ./doa4retail_manifest.toml)")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake" DOA4_REXGLUE_CMAKE)
# Bracket argument: match the literal ${CMAKE_CURRENT_SOURCE_DIR} text in the
# generated file, not its expansion.
string(REPLACE [[codegen ${CMAKE_CURRENT_SOURCE_DIR}/doa4retail_manifest.toml]]
               "codegen ./doa4retail_manifest.toml" DOA4_REXGLUE_CMAKE "${DOA4_REXGLUE_CMAKE}")
if(DOA4_REXGLUE_CMAKE MATCHES [[codegen \$\{CMAKE_CURRENT_SOURCE_DIR\}]])
    message(FATAL_ERROR "Could not rewrite the codegen manifest path in generated/rexglue.cmake; "
        "the SDK template changed shape. Non-ASCII install paths will fail until this is fixed.")
endif()
# Written into the build tree: rexglue.cmake uses no CMAKE_CURRENT_LIST_DIR and
# no relative include(), and keeping it out of the source tree avoids tripping
# CMake's CONFIGURE_DEPENDS glob checks on generated/.
file(WRITE "${CMAKE_BINARY_DIR}/rexglue.local.cmake" "${DOA4_REXGLUE_CMAKE}")
include("${CMAKE_BINARY_DIR}/rexglue.local.cmake")

# Sources
set(DOA4RETAIL_SOURCES
    src/main.cpp
    src/doa4_preview_cut.cpp
    src/doa4_preview_wind.cpp
    src/doa4_widescreen.cpp
    src/doa4_skip_intro.cpp
    src/doa4_attract_fade.cpp
    src/doa4_frame_probe.cpp
    src/doa4_freecam.cpp
    src/doa4_stage_debug.cpp
    src/doa4_all_stages.cpp
    src/doa4_glow_tuner.cpp
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
