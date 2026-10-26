#!/bin/sh
# Headless Ghidra analysis of the local XBE. The project, the log, and the
# address map are written under re/ghidra/ and are not committed.
#
# Needs Homebrew Ghidra (the formula; the old cask name is gone) and a JDK
# that matches it. Ghidra 12.2 wants Java 25. The XBE loader is
# https://github.com/XboxDev/ghidra-xbe built against that Ghidra. Headless
# Ghidra loads extension jars from the user settings directory, not from the
# Homebrew prefix:
#
#   ~/Library/ghidra/ghidra_<version>_PUBLIC/Extensions/<name>/
#
# Unzip the dist zip there. The published zip is built for an older Ghidra
# and will not load. Headless also needs that directory on
# -Dghidra.external.modules, which this script sets. The -loader value is the
# class name XbeLoader. Kernel imports are labeled by the loader. XDK library
# names come from XbSymbolDatabase inside that extension. This title has no
# .XTLID section, so the XTLID analyzer has nothing to read.
#
#   brew install ghidra openjdk@25
#   tools/re/analyze_xbe.sh assets/default.xbe
set -eu
cd "$(dirname "$0")/../.."

XBE="${1:-assets/default.xbe}"
if [ ! -f "$XBE" ]; then
    echo "missing $XBE" >&2
    echo "The XBE stays on this machine. Pass the path to your local default.xbe." >&2
    exit 1
fi

GHIDRA="${GHIDRA_INSTALL:-}"
if [ -z "$GHIDRA" ] && command -v brew >/dev/null 2>&1; then
    GHIDRA="$(brew --prefix ghidra 2>/dev/null || true)"
fi
if [ -z "$GHIDRA" ] || [ ! -d "$GHIDRA" ]; then
    echo "Ghidra is not installed. brew install ghidra" >&2
    exit 1
fi
HEADLESS="$GHIDRA/libexec/support/analyzeHeadless"
if [ ! -x "$HEADLESS" ]; then
    echo "analyzeHeadless is not at $HEADLESS" >&2
    exit 1
fi

if [ -z "${JAVA_HOME:-}" ] || [ ! -x "${JAVA_HOME}/bin/java" ]; then
    if [ -x /opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/java ]; then
        JAVA_HOME=/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home
    fi
fi
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "Set JAVA_HOME to a JDK 25 install. brew install openjdk@25" >&2
    exit 1
fi
export JAVA_HOME
export PATH="$JAVA_HOME/bin:$PATH"

VER="$(sed -n 's/^application.version=//p' "$GHIDRA/libexec/Ghidra/application.properties" | head -1)"
REL="$(sed -n 's/^application.release.name=//p' "$GHIDRA/libexec/Ghidra/application.properties" | head -1)"
USER_EXT="${HOME}/Library/ghidra/ghidra_${VER}_${REL}/Extensions"
LOADER_DIR=""
if [ -d "$USER_EXT" ]; then
    LOADER_DIR="$(find "$USER_EXT" -name extension.properties -print0 2>/dev/null \
        | xargs -0 grep -l "XboxExecutableLoader" 2>/dev/null \
        | head -1 | xargs dirname 2>/dev/null || true)"
fi
if [ -z "$LOADER_DIR" ] || [ ! -f "$LOADER_DIR/Module.manifest" ]; then
    echo "The XBE loader is not installed under $USER_EXT." >&2
    echo "Build https://github.com/XboxDev/ghidra-xbe against this Ghidra" >&2
    echo "(GHIDRA_INSTALL_DIR=$GHIDRA/libexec, JAVA_HOME=$JAVA_HOME) and unzip" >&2
    echo "the dist zip into that directory. A copy under the Homebrew prefix" >&2
    echo "is not on the headless classpath. The published zip tracks an older" >&2
    echo "Ghidra and will not load here." >&2
    exit 1
fi
# Headless matches Loader.getClass().getSimpleName(), which is XbeLoader.
# The module jar is indexed only when its directory is an external module.
case " ${GHIDRA_JAVA_OPTIONS:-} " in
    *" -Dghidra.external.modules="*) ;;
    *) GHIDRA_JAVA_OPTIONS="-Dghidra.external.modules=${LOADER_DIR}${GHIDRA_JAVA_OPTIONS:+ ${GHIDRA_JAVA_OPTIONS}}" ;;
esac
export GHIDRA_JAVA_OPTIONS

mkdir -p re/ghidra
XBE_ABS="$(cd "$(dirname "$XBE")" && pwd)/$(basename "$XBE")"
OUT="$(pwd)/re/ghidra/time-rng-map.txt"
"$HEADLESS" "$(pwd)/re/ghidra" FuzionFrenzy \
    -import "$XBE_ABS" \
    -overwrite \
    -loader XbeLoader \
    -scriptPath "$(pwd)/tools/re" \
    -postScript TimeRngMap.java "$OUT" \
    -analysisTimeoutPerFile 1800 \
    -log "$(pwd)/re/ghidra/analyze.log"
echo "map: $OUT"
echo "log: $(pwd)/re/ghidra/analyze.log"
