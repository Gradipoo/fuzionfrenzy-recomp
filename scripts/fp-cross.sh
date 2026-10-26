#!/bin/sh
# Hash the bundled guest math on every compiler and architecture this machine
# can run. The full game is not built here: Homebrew Vulkan on this Mac is
# arm64, so an x86_64 RingHost does not link.
set -eu
cd "$(dirname "$0")/.."

OLM_CFLAGS="-std=c99 -O2 -ffp-contract=off -fno-fast-math -fno-builtin -fno-strict-aliasing -fvisibility=hidden -DOLM_PRIVATE -D__BSD_VISIBLE -Wno-deprecated-non-prototype -I third_party/openlibm/include -I third_party/openlibm/src -include third_party/openlibm/olm_prefix.h"

compile_olm() {
    cc="$1"
    extra="$2"
    outdir="$3"
    mkdir -p "$outdir"
    while IFS= read -r name; do
        [ -z "$name" ] && continue
        base=$(basename "$name" .c)
        # shellcheck disable=SC2086
        $cc $OLM_CFLAGS $extra -c "third_party/openlibm/src/$name" -o "$outdir/$base.o"
    done < third_party/openlibm/sources.txt
}

build() {
    # $1 "arm64" | "x86_64" | "gcc", $2 output binary
    objs="$out/objs-$1"
    if [ "$1" = gcc ]; then
        compile_olm gcc-16 "" "$objs"
        g++-16 -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
            tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp "$objs"/*.o -o "$2"
    elif [ "$1" = x86_64 ]; then
        compile_olm clang "-arch x86_64 -mfpmath=sse -msse2" "$objs"
        clang++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
            -arch x86_64 -mfpmath=sse -msse2 \
            tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp "$objs"/*.o -o "$2"
    else
        compile_olm clang "-arch arm64" "$objs"
        clang++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
            -arch arm64 \
            tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp "$objs"/*.o -o "$2"
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
if command -v g++-16 >/dev/null 2>&1 && command -v gcc-16 >/dev/null 2>&1; then
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
