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

# Git is required only when a marker is missing. The launcher ships a
# pre-patched tree and does not ship git, so configure must not look for
# git until a patch is about to be applied. The resolved path is kept for
# later apply sites, and sdk is initialized once.
macro(xbe_ensure_git)
    if(NOT GIT_EXE)
        find_program(GIT_EXE git NO_CACHE)
        if(NOT GIT_EXE AND CMAKE_HOST_WIN32)
            set(_git_candidates
                "$ENV{ProgramFiles}/Git/cmd/git.exe"
                "$ENV{ProgramFiles}/Git/bin/git.exe")
            if(DEFINED ENV{ProgramW6432} AND NOT "$ENV{ProgramW6432}" STREQUAL "")
                list(APPEND _git_candidates "$ENV{ProgramW6432}/Git/cmd/git.exe")
            endif()
            list(APPEND _git_candidates "$ENV{LocalAppData}/Programs/Git/cmd/git.exe")
            foreach(_git_cand IN LISTS _git_candidates)
                if(EXISTS "${_git_cand}")
                    set(GIT_EXE "${_git_cand}")
                    break()
                endif()
            endforeach()
        endif()
        if(NOT GIT_EXE)
            message(FATAL_ERROR "git was not found. Install Git for Windows and choose Git from the command line, or install git and put it on PATH.")
        endif()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" rev-parse --is-inside-work-tree
                        RESULT_VARIABLE _is_git OUTPUT_QUIET ERROR_QUIET)
        if(NOT _is_git EQUAL 0)
            execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" init -q)
        endif()
    endif()
endmacro()

# softmath.h is created by the patch below. A clean tree does not have it yet,
# and file(READ) aborts the configure when the path is missing.
set(_hdr "")
if(EXISTS "${SDK}/runtime/cpu/softmath.h")
    file(READ "${SDK}/runtime/cpu/softmath.h" _hdr)
endif()
if(NOT _hdr MATCHES "XBE_LOCKSTEP_FP")
    xbe_ensure_git()
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
    xbe_ensure_git()
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
        xbe_ensure_git()
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
        xbe_ensure_git()
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
        xbe_ensure_git()
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
        xbe_ensure_git()
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
        xbe_ensure_git()
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
        xbe_ensure_git()
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
    xbe_ensure_git()
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
        xbe_ensure_git()
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
    xbe_ensure_git()
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
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_net}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-net.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-net.patch")
    endif()
endif()

# Run queue ordering behind FUZION_RUN_QUEUE. The project header lives in
# src/det/run_queue.h; the sdk build already has src on its include path.
# The relay patch below expects that include, so this lands first.
set(_runq "${REPO}/patches/sdk-lockstep-run-queue.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_runq}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _runq_src)
    if(NOT _runq_src MATCHES "XBE_RUN_QUEUE")
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_runq}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-run-queue.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-run-queue.patch")
    endif()
endif()

# Relay lobby on top of the net hook. With --lockstep and no peer, the
# first present waits for an armed RelayClient. A direct peer skips it.
set(_relay "${REPO}/patches/sdk-lockstep-relay.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_relay}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _relay_src)
    if(NOT _relay_src MATCHES "XBE_LOCKSTEP_RELAY")
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_relay}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-relay.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-relay.patch")
    endif()
endif()

# Keep presenting while four relay seats join. A direct peer skips it.
set(_lobby "${REPO}/patches/sdk-lockstep-lobby.patch")
if(EXISTS "${SDK}/runtime/core/determinism.cpp" AND EXISTS "${_lobby}")
    file(READ "${SDK}/runtime/core/determinism.cpp" _lobby_src)
    if(NOT _lobby_src MATCHES "XBE_LOCKSTEP_LOBBY")
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_lobby}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-lobby.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-lobby.patch")
    endif()
endif()

# Host, Join, and Copy run on the window thread. Text input does too.
set(_relay_ui "${REPO}/patches/sdk-lockstep-relay-ui.patch")
if(EXISTS "${SDK}/runtime/app/app.h" AND EXISTS "${_relay_ui}")
    file(READ "${SDK}/runtime/app/app.h" _relay_ui_src)
    if(NOT _relay_ui_src MATCHES "PostRelay")
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_relay_ui}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-lockstep-relay-ui.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-lockstep-relay-ui.patch")
    endif()
endif()

# Twisted System stage-1 skip and the MODULATE2X bias. The Apple patch
# already contains this. Windows and Linux archives start from the pin.
set(_stage "${REPO}/patches/sdk-ff-stage.patch")
if(EXISTS "${SDK}/runtime/gpu/shaders.cpp" AND EXISTS "${_stage}")
    file(READ "${SDK}/runtime/gpu/shaders.cpp" _stage_src)
    if(NOT _stage_src MATCHES "Skip the stage")
        xbe_ensure_git()
        execute_process(COMMAND "${GIT_EXE}" -C "${SDK}" apply --whitespace=nowarn "${_stage}"
                        RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "patches/sdk-ff-stage.patch failed (${_rc})")
        endif()
        message(STATUS "applied patches/sdk-ff-stage.patch")
    endif()
endif()
