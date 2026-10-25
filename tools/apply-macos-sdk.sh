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

git -C sdk apply --whitespace=nowarn ../patches/sdk-macos-arm64.patch
echo "applied patches/sdk-macos-arm64.patch"
