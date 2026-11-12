# Windows player build. The launcher sets RINGHOST_TOOLCHAIN and RINGHOST_WINSDK.
# CMake treats a backslash in a quoted string as an escape, so C:\Users\... is a
# syntax error (\U). Forward slashes are the same paths and are not escapes.
if(NOT DEFINED ENV{RINGHOST_TOOLCHAIN} OR NOT DEFINED ENV{RINGHOST_WINSDK})
    message(FATAL_ERROR "RINGHOST_TOOLCHAIN and RINGHOST_WINSDK must be set")
endif()
file(TO_CMAKE_PATH "$ENV{RINGHOST_TOOLCHAIN}" _tc)
file(TO_CMAKE_PATH "$ENV{RINGHOST_WINSDK}" _sdk)
if(NOT IS_DIRECTORY "${_sdk}/VC/Tools/MSVC" OR NOT IS_DIRECTORY "${_sdk}/Windows Kits/10/Lib")
    message(FATAL_ERROR "RINGHOST_WINSDK is not an xwin splat (${_sdk})")
endif()
# xwin ships the release CRT only. CMake's compiler test defaults to Debug and
# then looks for msvcrtd.lib, which is not in the splat.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_C_COMPILER "${_tc}/bin/clang-cl.exe")
set(CMAKE_CXX_COMPILER "${_tc}/bin/clang-cl.exe")
set(CMAKE_LINKER "${_tc}/bin/lld-link.exe")
set(CMAKE_AR "${_tc}/bin/llvm-lib.exe")
set(CMAKE_RC_COMPILER "${_tc}/bin/llvm-rc.exe")
# Windows-Clang searches for rc, then llvm-rc, unless this is already set.
# That search is not cached, so every compiler test walked Program Files.
set(CMAKE_RC_COMPILER_INIT "${_tc}/bin/llvm-rc.exe")
set(CMAKE_MT "${_tc}/bin/llvm-mt.exe")
# This toolchain has no clang-scan-deps. Leave the result unset and CMake
# searches the whole PATH on every try_compile.
set(CMAKE_C_COMPILER_CLANG_SCAN_DEPS "CMAKE_C_COMPILER_CLANG_SCAN_DEPS-NOTFOUND" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND" CACHE FILEPATH "" FORCE)
# #region agent log
set(_rh_dbg "C:/Mac/Home/Documents/projects/fuzionfrenzy-recomp/.cursor")
if(IS_DIRECTORY "${_rh_dbg}")
    string(TIMESTAMP _rh_ts "%s")
    file(APPEND "${_rh_dbg}/debug-871d62.log"
        "{\"sessionId\":\"871d62\",\"runId\":\"post-fix\",\"hypothesisId\":\"A\",\"location\":\"cmake/windows-player.cmake\",\"message\":\"toolchain tools pinned\",\"data\":{\"rcInit\":\"${CMAKE_RC_COMPILER_INIT}\",\"scanDeps\":\"${CMAKE_C_COMPILER_CLANG_SCAN_DEPS}\"},\"timestamp\":${_rh_ts}}\n")
endif()
# #endregion

# FLAGS_INIT is appended to CMake's MSVC defaults. Setting CMAKE_C_FLAGS from
# the command line would replace those defaults.
set(CMAKE_C_FLAGS_INIT "/winsysroot \"${_sdk}\" -march=x86-64-v2")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot \"${_sdk}\" -march=x86-64-v2")
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT "/winsysroot:\"${_sdk}\"")
endforeach()
