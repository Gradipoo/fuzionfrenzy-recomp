#!/bin/sh
# Hash the bundled guest math on every compiler and architecture this machine
# can run. The full game is not built here: Homebrew Vulkan on this Mac is
# arm64, so an x86_64 RingHost does not link.
set -eu
cd "$(dirname "$0")/.."

build() {
    # $1 arch or "gcc", $2 output
    if [ "$1" = gcc ]; then
        g++-16 -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
            tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp -o "$2"
    else
        clang++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
            -arch "$1" tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp -o "$2"
    fi
}

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

build arm64 "$out/arm64"
build x86_64 "$out/x64"
arm=$("$out/arm64")
x64=$(arch -x86_64 "$out/x64")
echo "clang arm64      $arm"
echo "clang x86_64     $x64"
fail=0
if [ "$arm" != "$x64" ]; then
    echo "FAIL clang arm64 and x86_64 differ"
    fail=1
fi
if command -v g++-16 >/dev/null 2>&1; then
    build gcc "$out/gcc"
    gcc_out=$("$out/gcc")
    echo "gcc arm64        $gcc_out"
    if [ "$arm" != "$gcc_out" ]; then
        echo "FAIL gcc and clang differ"
        fail=1
    fi
fi
if [ "$fail" -eq 0 ]; then
    echo "PASS"
fi
exit "$fail"
