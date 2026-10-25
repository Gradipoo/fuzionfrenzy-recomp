#!/usr/bin/env bash
# Fuzion Frenzy - native build (Vulkan + SDL3).
# Linux is the default preset there; macOS (Apple Silicon) defaults to
# macos-release. Mirrors build.bat: configure, then build the game.
# config/game.toml is already in the tree for this XBE.
set -euo pipefail

cd "$(dirname "$0")"
if [ "$(uname -s)" = "Darwin" ]; then
    PRESET="${1:-macos-release}"
else
    PRESET="${1:-linux-release}"
fi

GAME_XBE="default.xbe"
GAME_EXE="FuzionFrenzy"
OUT="out/build/${PRESET}"

echo
echo "  Fuzion Frenzy - build (${PRESET})"
echo "  ================================================"
echo

if [ ! -f "assets/${GAME_XBE}" ]; then
    echo "  Put the full game files in the assets folder first:" >&2
    echo "    $(pwd)/assets" >&2
    echo "  It needs ${GAME_XBE} with the game's folders beside it." >&2
    exit 1
fi

# toolchain
echo "[1/3] Toolchain"
MISSING=""
for tool in clang clang++ cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || MISSING="${MISSING} ${tool}"
done
if [ -n "$MISSING" ]; then
    echo "  missing:${MISSING}" >&2
    echo "  Install clang, cmake and ninja, then run this again." >&2
    exit 1
fi
clang++ --version | head -n 1
if [ "$(uname -s)" = "Darwin" ]; then
    if [ "$(uname -m)" != "arm64" ]; then
        echo "  macOS build is native arm64. This machine is $(uname -m)." >&2
        exit 1
    fi
    ./tools/apply-macos-sdk.sh
fi

# symbols
echo "[2/3] XDK symbols"
cmake --preset "${PRESET}" > /dev/null
if [ -f config/game.toml ]; then
    echo "      config/game.toml already present"
else
    cmake --build --preset "${PRESET}" --target xbe_symbols > /dev/null
    mkdir -p config
    "${OUT}/xbe_symbols" "assets/${GAME_XBE}" sdk/xdk --out config/game.toml
fi

# game
echo "[3/3] Game code"
echo "      first build recompiles ${GAME_XBE}, expect a long build ..."
cmake --build --preset "${PRESET}"

echo
echo "  Fuzion Frenzy built."
echo "  ${OUT}/${GAME_EXE}"
echo
