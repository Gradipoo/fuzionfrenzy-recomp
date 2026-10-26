# Fuzion Frenzy (original Xbox, USA), statically recompiled

cmake_minimum_required(VERSION 3.25)

# macOS arm64 support lives in patches/sdk-macos-arm64.patch, applied onto the
# pinned SDK submodule. Idempotent. Linux and Windows configure the submodule as-is.
if(APPLE)
    execute_process(
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply-macos-sdk.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _xbe_macos_patch)
    if(NOT _xbe_macos_patch EQUAL 0)
        message(FATAL_ERROR "tools/apply-macos-sdk.sh failed (${_xbe_macos_patch})")
    endif()
endif()

project(FuzionFrenzy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBERecomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBERecomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBERecomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
# host-tools preset: xbe_recomp and xbe_symbols alone, built for this machine. A cross build
# (windows-cross) runs xbe_recomp during the build and takes it from there (XBE_HOST_TOOLS_DIR).
option(XBE_HOST_TOOLS_ONLY "build only the SDK's host tools, not the game" OFF)
if(XBE_HOST_TOOLS_ONLY)
    include(${SDK}/cmake/XbeTools.cmake)
    return()
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat and release.bat, xbe = GAME_XBE; add_xbe_image reads
# assets/default.xbe and config/game.toml, and adds src/game/*.cpp to this image alone
add_xbe_image(FuzionFrenzy default.xbe game.toml ${GAME_PARTITIONS} game)
# The Mac process and window are Ring Host. Linux and Windows keep the
# target's original file name. The CMake target stays FuzionFrenzy so an
# existing macos-release build relinks instead of recompiling the guest.
if(APPLE)
    set_target_properties(FuzionFrenzy PROPERTIES OUTPUT_NAME RingHost)
endif()
