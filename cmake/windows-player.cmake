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

set(CMAKE_C_COMPILER "${_tc}/bin/clang-cl.exe")
set(CMAKE_CXX_COMPILER "${_tc}/bin/clang-cl.exe")
set(CMAKE_LINKER "${_tc}/bin/lld-link.exe")
set(CMAKE_AR "${_tc}/bin/llvm-lib.exe")
set(CMAKE_RC_COMPILER "${_tc}/bin/llvm-rc.exe")
set(CMAKE_MT "${_tc}/bin/llvm-mt.exe")

# FLAGS_INIT is appended to CMake's MSVC defaults. Setting CMAKE_C_FLAGS from
# the command line would replace those defaults.
set(CMAKE_C_FLAGS_INIT "/winsysroot \"${_sdk}\" -march=x86-64-v2")
set(CMAKE_CXX_FLAGS_INIT "/winsysroot \"${_sdk}\" -march=x86-64-v2")
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT "/winsysroot:\"${_sdk}\"")
endforeach()
