#!/bin/sh
# Apply the macOS arm64 patch onto the pinned sdk/ submodule. Safe to run twice.
set -eu
cd "$(dirname "$0")/.."

if [ ! -f sdk/runtime/kernel/memory.cpp ]; then
    echo "sdk/ is empty. Run: git submodule update --init sdk" >&2
    exit 1
fi

# GCController publishes a wired Series pad after SDL_Init. This second patch
# opens it on the main thread. Idempotent: the marker is inside input.cpp.
apply_gamepad_watch() {
    if grep -q "XBE_GAMEPAD_WATCH" sdk/runtime/core/input.cpp; then
        return 0
    fi
    if [ ! -f patches/sdk-macos-gamepad-events.patch ]; then
        echo "missing patches/sdk-macos-gamepad-events.patch" >&2
        exit 1
    fi
    git -C sdk apply --whitespace=nowarn ../patches/sdk-macos-gamepad-events.patch
    echo "applied patches/sdk-macos-gamepad-events.patch"
}

# An already-patched tree used to stop here, so a newer input.cpp hunk never
# landed. Wired Xbox One support lives in that file. Refresh it from the
# pinned commit plus the current patch when the hint is missing.
if grep -q "XBE_DARWIN_GUEST" sdk/runtime/kernel/memory.cpp; then
    if ! grep -q "SDL_HINT_JOYSTICK_HIDAPI_XBOX_ONE" sdk/runtime/core/input.cpp; then
        if ! git -C sdk cat-file -e HEAD:runtime/core/input.cpp 2>/dev/null; then
            echo "sdk/runtime/core/input.cpp is an older macOS patch and there is no pinned commit to refresh it." >&2
            echo "Reset the sdk submodule, or re-copy the port sources, then build again." >&2
            exit 1
        fi
        git -C sdk checkout -- runtime/core/input.cpp
        _xbe_input_patch="${TMPDIR:-/tmp}/xbe-input-$$.patch"
        awk '
            /^diff --git / {
                if (keep) exit
                keep = ($0 ~ /a\/runtime\/core\/input.cpp/)
            }
            keep { print }
        ' patches/sdk-macos-arm64.patch > "$_xbe_input_patch"
        git -C sdk apply --whitespace=nowarn "$_xbe_input_patch"
        rm -f "$_xbe_input_patch"
        echo "refreshed sdk/runtime/core/input.cpp from patches/sdk-macos-arm64.patch"
    fi
    apply_gamepad_watch
    exit 0
fi

if [ ! -f patches/sdk-macos-arm64.patch ]; then
    echo "missing patches/sdk-macos-arm64.patch" >&2
    exit 1
fi

# A git checkout applies inside the submodule. A packaged port has the same
# sources and no submodule metadata, so it needs a work tree first. Linux
# never runs this script; the submodule path above is unchanged.
if git -C sdk rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C sdk apply --whitespace=nowarn ../patches/sdk-macos-arm64.patch
else
    git -C sdk init -q
    git -C sdk apply --whitespace=nowarn ../patches/sdk-macos-arm64.patch
fi
echo "applied patches/sdk-macos-arm64.patch"
apply_gamepad_watch
