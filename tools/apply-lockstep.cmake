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

# softmath.h is created by the patch below. A clean tree does not have it yet,
# and file(READ) aborts the configure when the path is missing.
set(_hdr "")
if(EXISTS "${SDK}/runtime/cpu/softmath.h")
    file(READ "${SDK}/runtime/cpu/softmath.h" _hdr)
endif()
if(NOT _hdr MATCHES "XBE_LOCKSTEP_FP")
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${PATCH}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "patches/sdk-lockstep.patch failed (${_rc})")
    endif()
    message(STATUS "applied patches/sdk-lockstep.patch")
endif()

# Linux and Windows do not apply the Apple patch, so determinism.cpp is
# missing. One patch adds the same runtime and call sites the Mac already
# has after the harness and the load-yield patches. A tree that already
# has the file (the Apple path) skips this and takes those two patches.
set(_host "${REPO}/patches/sdk-lockstep-host.patch")
if(NOT EXISTS "${SDK}/runtime/core/determinism.cpp")
    if(NOT EXISTS "${_host}")
        message(FATAL_ERROR "missing patches/sdk-lockstep-host.patch")
    endif()
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_host}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "patches/sdk-lockstep-host.patch failed (${_rc})")
    endif()
    message(STATUS "applied patches/sdk-lockstep-host.patch")
endif()

# Harness delta on top of the Apple patch's determinism.cpp. The host patch
# above already contains this marker, so it is skipped there.
set(_harness "${REPO}/patches/sdk-lockstep-harness.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp")
    file(READ "${SDK}/runtime/core/determinism.cpp" _det)
    if(NOT _det MATCHES "XBE_LOCKSTEP_HARNESS")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_harness}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-harness.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-harness.patch")
    endif()
endif()

# Present yields so a background load runs under fixed step. Applies on top of
# the harness. A tree with no determinism.cpp skips it.
set(_load "${REPO}/patches/sdk-lockstep-load-yield.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_load}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _load_src)
    if(NOT _load_src MATCHES "XBE_LOAD_YIELD")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_load}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-load-yield.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-load-yield.patch")
    endif()
endif()

# Fixed-step input IRPs stay queued until the next present drains them, after
# that frame's poll and before the state hash. A wait drains first so device
# open can be signalled. The host patch already has the load-yield marker, so
# this follows that patch there and follows the load-yield patch on Mac.
set(_input "${REPO}/patches/sdk-lockstep-input-dpc.patch")
if(EXISTS "${SDK}/runtime/kernel/objects.cpp" AND EXISTS "${_input}")
    file(READ "${SDK}/runtime/kernel/objects.cpp" _dpc_src)
    if(NOT _dpc_src MATCHES "XBE_INPUT_DPC")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_input}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-input-dpc.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-input-dpc.patch")
    endif()
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
