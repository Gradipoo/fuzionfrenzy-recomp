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

# Env-gated hardware watch (FUZION_WATCH_VA). Off unless that variable is set.
# The host patch's determinism.cpp does not have the marker, and neither does
# the Mac tree until this patch lands, so both chains apply it.
set(_watch "${REPO}/patches/sdk-lockstep-watch.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_watch}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _watch_src)
    if(NOT _watch_src MATCHES "XBE_WATCH_VA")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_watch}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-watch.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-watch.patch")
    endif()
endif()

# A guest thread that returns must store its exit signal before another thread
# can present. Otherwise the presenter hashes a still-clear signal on a slow host.
set(_texit "${REPO}/patches/sdk-lockstep-thread-exit.patch")
if(EXISTS "${SDK}/runtime/kernel/threads.cpp" AND EXISTS "${_texit}")
    file(READ "${SDK}/runtime/kernel/threads.cpp" _texit_src)
    if(NOT _texit_src MATCHES "XBE_THREAD_EXIT")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_texit}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-thread-exit.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-thread-exit.patch")
    endif()
endif()

# XInputGetState's packet number compares report fields. A memcmp of the whole
# Pad also saw the padding after connected, which is not stable across hosts.
set(_xinput "${REPO}/patches/sdk-lockstep-xinput-packet.patch")
if(EXISTS "${SDK}/runtime/xdk/xapi.cpp" AND EXISTS "${_xinput}")
    file(READ "${SDK}/runtime/xdk/xapi.cpp" _xin_src)
    if(NOT _xin_src MATCHES "XBE_XINPUT_PACKET")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_xinput}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-xinput-packet.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-xinput-packet.patch")
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

# Snapshot resume. One patch for the shared files. The guest-window hint is
# a second patch because Apple's memory.cpp and the Linux one do not match.
set(_snap "${REPO}/patches/sdk-lockstep-snapshot.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_snap}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _snap_src)
    if(NOT _snap_src MATCHES "XBE_SNAPSHOT")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_snap}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-snapshot.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-snapshot.patch")
    endif()
endif()
file(READ "${SDK}/runtime/kernel/memory.cpp" _snap_mem)
if(NOT _snap_mem MATCHES "SetBaseHint")
    if(_snap_mem MATCHES "HostProtect")
        set(_snap_mem_patch "${REPO}/patches/sdk-lockstep-snapshot-memory-apple.patch")
    else()
        set(_snap_mem_patch "${REPO}/patches/sdk-lockstep-snapshot-memory.patch")
    endif()
    execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_snap_mem_patch}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${_snap_mem_patch} failed (${_rc})")
    endif()
    message(STATUS "applied ${_snap_mem_patch}")
endif()

# Delay lockstep. The session lives in src/net. This hook calls it.
set(_net "${REPO}/patches/sdk-lockstep-net.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_net}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _net_src)
    if(NOT _net_src MATCHES "XBE_LOCKSTEP_NET")
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_net}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-net.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-net.patch")
    endif()
endif()
