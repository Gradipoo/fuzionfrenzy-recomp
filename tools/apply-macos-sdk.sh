#!/bin/sh
# Apply the macOS arm64 patch onto the pinned sdk/ submodule. Safe to run twice.
set -eu
cd "$(dirname "$0")/.."

if [ ! -f sdk/runtime/kernel/memory.cpp ]; then
    echo "sdk/ is empty. Run: git submodule update --init sdk" >&2
    exit 1
fi

if grep -q "XBE_DARWIN_GUEST" sdk/runtime/kernel/memory.cpp; then
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
