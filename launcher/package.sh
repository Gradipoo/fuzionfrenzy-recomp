#!/bin/sh
# Pack the Linux or Windows Ring Host launcher with the port sources.
# The archive does not hold a recompiled game binary, assets/, generated/, or a disc.
# The Mac app is macos/launcher/package-dmg.sh.
set -eu

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
DIST="$HERE/dist"
BUILD="$DIST/build"

os=$(uname -s)
case "$os" in
    Linux)
        NAME="RingHost-linux-x64"
        BIN_NAME="RingHostLauncher"
        ARCHIVE="$DIST/$NAME.tar.zst"
        ;;
    MINGW*|MSYS*|CYGWIN*|Windows*)
        NAME="RingHost-win64"
        BIN_NAME="RingHostLauncher.exe"
        ARCHIVE="$DIST/$NAME.zip"
        ;;
    *)
        echo "package.sh packs the Linux and Windows launcher. This machine is $os $(uname -m)." >&2
        echo "On a Mac, run macos/launcher/package-dmg.sh." >&2
        exit 1
        ;;
esac

if [ ! -e "$ROOT/sdk/.git" ]; then
    echo "sdk submodule is not checked out. Run: git submodule update --init sdk" >&2
    exit 1
fi

mkdir -p "$DIST"
echo "configuring launcher"
if [ "$os" = "Linux" ] && command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    export CC=clang CXX=clang++
fi
cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel

BIN=""
for candidate in "$BUILD/$BIN_NAME" "$BUILD/Release/$BIN_NAME"; do
    if [ -f "$candidate" ]; then
        BIN=$candidate
        break
    fi
done
if [ -z "$BIN" ]; then
    echo "build did not produce $BIN_NAME" >&2
    exit 1
fi

REV=$(git -C "$ROOT" rev-parse --short HEAD)
# The sdk worktree may be patched on a Mac checkout. That line is not part of
# the version stamp. Linux and Windows checkouts leave sdk at the pin.
DIRTY_TREE=$(git -C "$ROOT" status --porcelain --untracked-files=no | grep -v '^.\{1,2\} sdk$' || true)
if [ -n "$DIRTY_TREE" ]; then DIRTY="-dirty"; else DIRTY=""; fi
VERSION="$(tr -d '[:space:]' < "$ROOT/macos/launcher/PORT_VERSION")+${REV}${DIRTY}"

STAGE="$DIST/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/port"
cp "$BIN" "$STAGE/$BIN_NAME"
printf '%s\n' "$VERSION" > "$STAGE/port-version"

echo "packing port sources ($VERSION)"
# git archive is the committed tree, not the working copy. A Windows checkout
# would otherwise rewrite script line endings before the copy.
rm -rf "$STAGE/port"
mkdir -p "$STAGE/port"
git -C "$ROOT" archive --format=tar HEAD | tar -C "$STAGE/port" -xf -
rm -rf "$STAGE/port/sdk" "$STAGE/port/out" "$STAGE/port/generated" "$STAGE/port/assets" "$STAGE/port/macos/launcher/dist"
mkdir -p "$STAGE/port/sdk"
git -C "$ROOT/sdk" archive --format=tar HEAD | tar -C "$STAGE/port/sdk" -xf -
chmod +x "$STAGE/port/build.sh" "$STAGE/port/tools/apply-macos-sdk.sh" 2>/dev/null || true

if find "$STAGE/port" \( \
        -name '*.xbe' -o -name '*.iso' -o -name '*.xiso' -o \
        -name 'x86_recomp*.cpp' -o \
        -name 'FuzionFrenzy' -o -name 'FuzionFrenzy.exe' -o \
        -name 'RingHost' -o -name 'RingHost.exe' \
    \) -type f | grep -q .; then
    echo "refusing to package game-derived files:" >&2
    find "$STAGE/port" \( \
        -name '*.xbe' -o -name '*.iso' -o -name '*.xiso' -o \
        -name 'x86_recomp*.cpp' -o \
        -name 'FuzionFrenzy' -o -name 'FuzionFrenzy.exe' -o \
        -name 'RingHost' -o -name 'RingHost.exe' \
    \) -type f >&2
    exit 1
fi
if [ ! -f "$STAGE/port/build.sh" ] || [ ! -f "$STAGE/port/build.bat" ] || [ ! -f "$STAGE/port/sdk/runtime/kernel/memory.cpp" ]; then
    echo "port payload is incomplete" >&2
    exit 1
fi

sh "$ROOT/tools/release/fetch-toolchain.sh" "$STAGE/port/toolchain"

cat > "$STAGE/ReadMe.txt" <<'EOF'
Ring Host

This folder is a launcher. It plays a legally owned copy of the original
Xbox game Fuzion Frenzy (USA). It does not contain that game, a recompiled
binary, or any art from the disc.

On first launch, choose your own USA disc image (.iso) or a folder that
already holds default.xbe plus the rest of the extracted disc. The launcher
checks that it is the release this port supports, then compiles the game on
this machine.

Linux keeps the built game in ~/.local/share/ringhost
(or $XDG_DATA_HOME/ringhost). Windows keeps it in %LOCALAPPDATA%\RingHost.

The compiler and libraries are in this folder. Linux needs a Vulkan
graphics driver. No other packages.

On Windows, the first launch downloads the Microsoft Windows SDK into
%LOCALAPPDATA%\RingHost\winsdk and accepts that SDK license. Later launches
stay offline. No Visual Studio install.

The first compile is local and takes a while. Later launches reuse the
build until this copy's version changes.
EOF

# The archive records this clock as local time. A UTC runner is hours ahead
# of a US Windows clock, and Ninja then reconfigures forever.
find "$STAGE" -exec touch -d '2020-01-01 00:00:00' {} +

rm -f "$ARCHIVE"
if [ "$NAME" = "RingHost-linux-x64" ]; then
    if ! tar --zstd -C "$DIST" -cf "$ARCHIVE" "$NAME"; then
        echo "tar --zstd failed. Install zstd." >&2
        exit 1
    fi
    archive_bytes=$(wc -c < "$ARCHIVE" | tr -d ' ')
    if [ "$archive_bytes" -ge 2147483648 ]; then
        echo "archive is $archive_bytes bytes. GitHub's limit is 2147483648." >&2
        exit 1
    fi
else
    py=python3
    if ! command -v python3 >/dev/null 2>&1; then
        py=python
    fi
    "$py" - "$STAGE" "$ARCHIVE" <<'PY'
import os, sys, zipfile
stage, dest = sys.argv[1], sys.argv[2]
root = os.path.dirname(os.path.abspath(stage))
with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for dirpath, _, names in os.walk(stage):
        for name in names:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace("\\", "/")
            archive.write(full, rel)
PY
fi

echo
echo "stage   $STAGE"
echo "archive $ARCHIVE"
echo "version $VERSION"
