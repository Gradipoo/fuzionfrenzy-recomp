# Fuzion Frenzy (original Xbox, USA), statically recompiled

cmake_minimum_required(VERSION 3.25)

# macOS arm64 support lives in patches/sdk-macos-arm64.patch, applied onto the
# pinned SDK submodule. Idempotent. Linux and Windows do not apply it.
if(APPLE)
    execute_process(
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply-macos-sdk.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _xbe_macos_patch)
    if(NOT _xbe_macos_patch EQUAL 0)
        message(FATAL_ERROR "tools/apply-macos-sdk.sh failed (${_xbe_macos_patch})")
    endif()
endif()
# Cross-platform lockstep math and fresh-commit zeroing. Every host applies
# this, after the Apple patch when there is one. Idempotent.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply-lockstep.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _xbe_lockstep_patch)
if(NOT _xbe_lockstep_patch EQUAL 0)
    message(FATAL_ERROR "tools/apply-lockstep.cmake failed (${_xbe_lockstep_patch})")
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
# Wired Xbox One pads have no macOS kernel driver, and GameController only
# opens them over Bluetooth. SDL's HIDAPI Xbox One driver opens the USB
# device through libusb in userspace. Linking the Homebrew dylib records
# its absolute install name. SDL_HIDAPI_LIBUSB_SHARED would dlopen the leaf
# name "libusb-1.0.dylib", which dyld does not search for under Homebrew.
# Linux keeps SDL's own choice (hidraw, and libusb only when it is present).
# ASan + UBSan for the runtime, vendored SDL/imgui/openlibm, and the game.
# macOS preset: macos-asan. Guest translation units opt out of two checks
# below; those are x86 semantics, not host bugs. See docs/sanitizers.md.
option(XBE_CI_RUNTIME "Compile the SDK runtime without a game image" OFF)
option(XBE_SANITIZE "Instrument the runtime and the game with ASan and UBSan" OFF)
if(XBE_SANITIZE)
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "XBE_SANITIZE needs Clang or GCC (ASan + UBSan).")
    endif()
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
    find_path(XBE_LIBUSB_INCLUDE libusb.h PATH_SUFFIXES libusb-1.0)
    find_library(XBE_LIBUSB_LIBRARY NAMES usb-1.0)
    if(NOT XBE_LIBUSB_INCLUDE OR NOT XBE_LIBUSB_LIBRARY)
        message(FATAL_ERROR "Wired Xbox controllers on macOS need libusb (userspace, no kernel extension). Install it with: brew install libusb")
    endif()
    set(SDL_HIDAPI ON CACHE BOOL "HIDAPI inside the vendored SDL3" FORCE)
    set(SDL_HIDAPI_LIBUSB ON CACHE BOOL "HIDAPI opens Xbox pads through libusb" FORCE)
    set(SDL_HIDAPI_LIBUSB_SHARED OFF CACHE BOOL "Link libusb instead of dlopen(libusb-1.0.dylib)" FORCE)
endif()
# CI compiles the runtime. It does not open a disc image, so SDL does not
# need a window system. These have to be set before SDL is fetched.
if(XBE_CI_RUNTIME AND NOT APPLE)
    set(SDL_X11 OFF CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

# Compile the runtime with no disc image and no recompiled guest. CI uses
# this. The symbol header is synthetic (tools/ci/stub_symbols.py) and lives
# in the build directory. It is not a substitute for a real image.
if(XBE_CI_RUNTIME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/stub_symbols.py")
    set(_xbe_ci_syms "${CMAKE_BINARY_DIR}/ci-symbols/image_symbols.h")
    execute_process(
        COMMAND python3
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/stub_symbols.py" "${_xbe_ci_syms}"
        RESULT_VARIABLE _xbe_ci_stub)
    if(NOT _xbe_ci_stub EQUAL 0)
        message(FATAL_ERROR "tools/ci/stub_symbols.py failed (${_xbe_ci_stub})")
    endif()
    add_library(xbe_runtime OBJECT ${XBE_RUNTIME_SOURCES})
    target_include_directories(xbe_runtime BEFORE PRIVATE
        "${CMAKE_BINARY_DIR}/ci-symbols"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SDK}/runtime")
    target_include_directories(xbe_runtime SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${XBE_SHADER_INCLUDES})
    target_compile_definitions(xbe_runtime PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS ${XBE_SHADER_DEFINES})
    target_link_libraries(xbe_runtime PRIVATE tomlplusplus::tomlplusplus SDL3::SDL3-static Vulkan::Vulkan ${XBE_SHADER_LIBS} ZLIB::ZLIB)
    if(MSVC)
        target_compile_options(xbe_runtime PRIVATE /fp:precise)
    else()
        target_compile_options(xbe_runtime PRIVATE -ffp-contract=off -fno-fast-math -fno-strict-aliasing)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
            target_compile_options(xbe_runtime PRIVATE -mfpmath=sse -msse2)
        endif()
    endif()
    return()
endif()

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
if(XBE_SANITIZE)
    # Recompiled x86, compiled as C++. Signed overflow wraps on the Xbox.
    # X86_MEM* uses aligned(1) types; X86_PTR casts to a host type whose
    # alignment the guest did not require. The file already lists -O2; the
    # last -O flag wins, and -O1 is what keeps this build able to reach a
    # minigame under the sanitizers.
    set(_xbe_guest_src "")
    math(EXPR _xbe_guest_last "${GAME_PARTITIONS} - 1")
    foreach(_i RANGE ${_xbe_guest_last})
        list(APPEND _xbe_guest_src
            "${CMAKE_CURRENT_SOURCE_DIR}/generated/game/x86_recomp.${_i}.cpp")
    endforeach()
    list(APPEND _xbe_guest_src
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/game/x86_func_mapping.cpp")
    set_property(SOURCE ${_xbe_guest_src} APPEND PROPERTY COMPILE_OPTIONS
        -O1 -fwrapv -fno-sanitize=alignment)
endif()
# Guest floating point has to match across arm64 and x86-64. No FMA contraction,
# no fast-math, and no 80-bit x87 evaluation of float or double. clang-cl still
# contracts under /fp:precise, so it gets the clang flag as well. GCC 16
# contracts at -O2 unless -ffp-contract=off is passed.
if(MSVC)
    target_compile_options(FuzionFrenzy PRIVATE /fp:precise)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(FuzionFrenzy PRIVATE /clang:-ffp-contract=off /clang:-fno-fast-math)
    endif()
else()
    target_compile_options(FuzionFrenzy PRIVATE -ffp-contract=off -fno-fast-math)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(FuzionFrenzy PRIVATE -mfpmath=sse -msse2)
    endif()
endif()

# Guest transcendentals. Portable C from openlibm, symbols renamed olm_*,
# no arch asm and no long-double objects. sqrt at the FSQRT site stays hardware.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openlibm/sources.txt" OLM_SRC_NAMES)
set(OLM_SRCS "")
foreach(n ${OLM_SRC_NAMES})
    list(APPEND OLM_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openlibm/src/${n}")
endforeach()
add_library(openlibm STATIC ${OLM_SRCS})
target_include_directories(openlibm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openlibm/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openlibm/src")
target_compile_definitions(openlibm PRIVATE OLM_PRIVATE __BSD_VISIBLE)
set_target_properties(openlibm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
set(OLM_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openlibm/olm_prefix.h")
if(MSVC)
    target_compile_options(openlibm PRIVATE /fp:precise)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(openlibm PRIVATE
            /clang:-ffp-contract=off /clang:-fno-fast-math /clang:-fno-builtin
            /clang:-fno-strict-aliasing /clang:-fvisibility=hidden
            /clang:-Wno-deprecated-non-prototype
            "/clang:-include${OLM_PREFIX}")
    else()
        target_compile_options(openlibm PRIVATE "/FI${OLM_PREFIX}")
    endif()
else()
    target_compile_options(openlibm PRIVATE
        -ffp-contract=off -fno-fast-math -fno-builtin -fno-strict-aliasing
        -fvisibility=hidden -Wno-deprecated-non-prototype
        "-include${OLM_PREFIX}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(openlibm PRIVATE -mfpmath=sse -msse2)
    endif()
endif()
if(XBE_SANITIZE)
    # fdlibm updates exponents with signed shifts and wrapping adds. Those
    # are the bit operations the math digest already pins. -fwrapv defines
    # the adds. The shift check is off for this library only; e_pow.c and
    # e_powf.c shift through unsigned so the reported site is defined in
    # every build. Float divide-by-zero here is IEEE, which is the result
    # the guest is supposed to see. The runtime stays fully checked.
    target_compile_options(openlibm PRIVATE
        -fwrapv
        -fno-sanitize=shift,signed-integer-overflow,float-divide-by-zero)
endif()
target_link_libraries(FuzionFrenzy PRIVATE openlibm)
