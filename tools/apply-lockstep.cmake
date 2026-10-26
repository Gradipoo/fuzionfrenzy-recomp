# Apply the cross-platform lockstep patch onto sdk/. Safe to run twice.
# Mac runs this after patches/sdk-macos-arm64.patch. Linux and Windows run it
# on the pinned submodule alone. The memory hunk differs on Apple, so it is a
# second patch chosen from the tree that is already on disk.
cmake_minimum_required(VERSION 3.25)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SDK "${REPO}/sdk")
set(PATCH "${REPO}/patches/sdk-lockstep.patch")

if(NOT EXISTS "${SDK}/runtime/kernel/memory.cpp")
    message(FATAL_ERROR "sdk/ is empty. Run: git submodule update --init sdk")
endif()
if(NOT EXISTS "${PATCH}")
    message(FATAL_ERROR "missing patches/sdk-lockstep.patch")
endif()

find_program(GIT_EXE git REQUIRED)
execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" rev-parse --is-inside-work-tree
                RESULT_VARIABLE _is_git OUTPUT_QUIET ERROR_QUIET)
if(NOT _is_git EQUAL 0)
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" init -q)
endif()

file(READ "${SDK}/runtime/cpu/softmath.h" _hdr)
if(NOT _hdr MATCHES "XBE_LOCKSTEP_FP")
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${PATCH}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "patches/sdk-lockstep.patch failed (${_rc})")
    endif()
    message(STATUS "applied patches/sdk-lockstep.patch")
endif()

file(READ "${SDK}/runtime/kernel/memory.cpp" _mem)
if(NOT _mem MATCHES "XBE_LOCKSTEP_COMMIT")
    if(_mem MATCHES "HostProtect")
        set(_mpatch "${REPO}/patches/sdk-lockstep-memory-apple.patch")
    else()
        set(_mpatch "${REPO}/patches/sdk-lockstep-memory.patch")
    endif()
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_mpatch}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${_mpatch} failed (${_rc})")
    endif()
    message(STATUS "applied ${_mpatch}")
endif()
