#!/bin/sh
# Build Fuzion Frenzy.app and a compressed disk image.
# The image holds the launcher and the port sources. It does not hold a
# recompiled game binary, assets/, generated/, or a disc.
# Ad-hoc signed. Notarization is future work.
set -eu

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
DIST="$HERE/dist"
APP="$DIST/Fuzion Frenzy.app"
DMG="$DIST/FuzionFrenzy-macos-launcher.dmg"

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
    echo "package-dmg.sh builds an Apple Silicon app. This machine is $(uname -s) $(uname -m)." >&2
    exit 1
fi

SDK=$(xcrun --show-sdk-path)
REV=$(git -C "$ROOT" rev-parse --short HEAD)
# The sdk worktree stays patched on a Mac checkout. That local submodule
# state is not part of the disk image (the image archives the pinned commit),
# so it does not change the version stamp.
DIRTY_TREE=$(git -C "$ROOT" status --porcelain --untracked-files=no | grep -v '^.\{1,2\} sdk$' || true)
if [ -n "$DIRTY_TREE" ]; then DIRTY="-dirty"; else DIRTY=""; fi
VERSION="$(tr -d '[:space:]' < "$HERE/PORT_VERSION")+${REV}${DIRTY}"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

echo "compiling launcher"
swiftc -parse-as-library -O -swift-version 5 \
    -target arm64-apple-macosx14.0 \
    -sdk "$SDK" \
    -framework SwiftUI -framework AppKit -framework CryptoKit \
    -o "$APP/Contents/MacOS/FuzionFrenzyLauncher" \
    "$HERE"/Sources/*.swift

cp "$HERE/Info.plist" "$APP/Contents/Info.plist"
printf '%s\n' "$VERSION" > "$APP/Contents/Resources/port-version"
cp "$HERE/ReadMe.txt" "$APP/Contents/Resources/ReadMe.txt"

echo "packing port sources ($VERSION)"
PORT="$APP/Contents/Resources/port"
mkdir -p "$PORT"
# Tracked port files only. sdk is a submodule gitlink, so it is filled from
# the pinned commit afterwards, without this machine's patched worktree.
git -C "$ROOT" ls-files -z | rsync -a --from0 --files-from=- "$ROOT/" "$PORT/"
rm -rf "$PORT/sdk"
mkdir -p "$PORT/sdk"
git -C "$ROOT/sdk" archive --format=tar HEAD | tar -C "$PORT/sdk" -xf -
chmod +x "$PORT/build.sh" "$PORT/tools/apply-macos-sdk.sh"

if find "$PORT" \( -name '*.xbe' -o -name '*.iso' -o -name '*.xiso' -o -name 'x86_recomp*.cpp' -o -name 'FuzionFrenzy' -type f \) | grep -q .; then
    echo "refusing to package game-derived files:" >&2
    find "$PORT" \( -name '*.xbe' -o -name '*.iso' -o -name '*.xiso' -o -name 'x86_recomp*.cpp' \) >&2
    exit 1
fi
if [ ! -f "$PORT/build.sh" ] || [ ! -f "$PORT/sdk/runtime/kernel/memory.cpp" ] || [ ! -f "$PORT/patches/sdk-macos-arm64.patch" ]; then
    echo "port payload is incomplete" >&2
    exit 1
fi

echo "ad-hoc signing"
codesign --force --sign - "$APP/Contents/MacOS/FuzionFrenzyLauncher"
codesign --force --sign - "$APP"
codesign --verify --verbose=2 "$APP"

STAGE=$(mktemp -d)
cp -R "$APP" "$STAGE/Fuzion Frenzy.app"
cp "$HERE/ReadMe.txt" "$STAGE/Read Me.txt"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Fuzion Frenzy" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

echo
echo "app  $APP"
echo "dmg  $DMG"
echo "version $VERSION"
echo "notarization is not done; this signature is ad-hoc"
