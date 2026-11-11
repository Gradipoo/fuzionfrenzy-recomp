#!/bin/sh
# Hash the bundled guest math on this compiler and this architecture.
# Does not build the game and does not read a disc image.
# FP_VALGRIND=1 runs the digest under valgrind (Linux). That is the
# uninitialized-use check: MemorySanitizer needs an instrumented libc.
set -eu
cd "$(dirname "$0")/.."

# The pinned sdk commit does not contain the lockstep math. Apply the same
# patches a configure would, so this hash is the game's hash.
if [ "$(uname -s)" = "Darwin" ]; then
    sh tools/apply-macos-sdk.sh
fi
cmake -P tools/apply-lockstep.cmake

expect=$(tr -d '[:space:]' < tools/fp-digest.expected)
if [ -z "$expect" ]; then
    echo "fp-digest: tools/fp-digest.expected is empty" >&2
    exit 1
fi

arch=$(uname -m)
extra=""
case "$arch" in
    x86_64|amd64) extra="-mfpmath=sse -msse2" ;;
    arm64|aarch64) extra="" ;;
    *) echo "fp-digest: unsupported architecture $arch" >&2; exit 1 ;;
esac

if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
    echo "fp-digest: clang and clang++ are required" >&2
    exit 1
fi

OLM_CFLAGS="-std=c99 -O2 -ffp-contract=off -fno-fast-math -fno-builtin -fno-strict-aliasing -fvisibility=hidden -DOLM_PRIVATE -D__BSD_VISIBLE -Wno-deprecated-non-prototype -I third_party/openlibm/include -I third_party/openlibm/src -include third_party/openlibm/olm_prefix.h"

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
while IFS= read -r name; do
    name=${name%$'\r'}
    [ -z "$name" ] && continue
    base=$(basename "$name" .c)
    # shellcheck disable=SC2086
    clang $OLM_CFLAGS $extra -c "third_party/openlibm/src/$name" -o "$out/$base.o"
done < third_party/openlibm/sources.txt

# shellcheck disable=SC2086
clang++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Isdk/runtime \
    $extra \
    tools/fp-digest.cpp sdk/runtime/cpu/softmath.cpp "$out"/*.o -o "$out/fp-digest"

got=$("$out/fp-digest")
hash=$(printf '%s\n' "$got" | awk 'NR==1 { print $1; exit }')
echo "fp-digest: $arch $got"
if [ "$hash" != "$expect" ]; then
    echo "fp-digest: FAIL $hash want $expect" >&2
    exit 1
fi
case "$got" in
    *"PASS"*) ;;
    *) echo "fp-digest: FAIL the digest binary did not pass" >&2; exit 1 ;;
esac

if [ "${FP_VALGRIND:-}" = 1 ]; then
    if ! command -v valgrind >/dev/null 2>&1; then
        echo "fp-digest: valgrind is not installed" >&2
        exit 1
    fi
    valgrind --error-exitcode=99 --leak-check=no --track-origins=yes \
        --quiet "$out/fp-digest" >/dev/null
    echo "fp-digest: valgrind PASS"
fi
echo "fp-digest: PASS $hash"
