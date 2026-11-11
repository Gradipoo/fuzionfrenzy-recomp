#!/bin/sh
# Fetch a player toolchain into <dest>.
# Host tools: curl, tar, unzip, git, cmake, ninja, and a C++ compiler.
# Windows only needs curl, tar or unzip, and git. It does not build the prefix.
#
# Usage: sh tools/release/fetch-toolchain.sh <dest>
# If dest/bin/cmake and dest/third_party/SDL/CMakeLists.txt already exist, exit 0.
# Those two are written last, so a partial dest is fetched again.

set -eu

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    echo "usage: sh tools/release/fetch-toolchain.sh <dest>" >&2
    exit 1
fi

mkdir -p "$1"
DEST=$(CDPATH= cd -- "$1" && pwd)

if [ -e "$DEST/bin/cmake" ] && [ -f "$DEST/third_party/SDL/CMakeLists.txt" ]; then
    exit 0
fi

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
# shellcheck disable=SC1091
. "$HERE/pins.env"

uname_s=$(uname -s)
case "$uname_s" in
    Linux) OS=linux ;;
    Darwin) OS=darwin ;;
    MINGW*|MSYS*|CYGWIN*|Windows*) OS=windows ;;
    *)
        echo "unsupported OS: $uname_s" >&2
        exit 1
        ;;
esac

arch=$(uname -m)
case "$OS:$arch" in
    linux:x86_64|darwin:arm64|windows:x86_64|windows:amd64) ;;
    *)
        echo "no pinned toolchain for $uname_s $arch" >&2
        exit 1
        ;;
esac

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing host tool: $1" >&2
        exit 1
    fi
}

require_cmd curl
require_cmd tar
require_cmd unzip
require_cmd git
if [ "$OS" != windows ]; then
    require_cmd cmake
    require_cmd ninja
    if ! command -v c++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
        echo "missing host C++ compiler" >&2
        exit 1
    fi
    if ! command -v cc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
        echo "missing host C compiler" >&2
        exit 1
    fi
fi

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
if [ -z "${JOBS:-}" ]; then
    JOBS=4
fi

PREFIX="$DEST/prefix"
WORK="$DEST/.fetch-tmp"
rm -rf "$WORK"
mkdir -p "$WORK" "$DEST/bin" "$DEST/third_party"
trap 'rm -rf "$WORK"' EXIT INT TERM

fetch_extract() {
    url=$1
    echo "fetch $url"
    rm -rf "$WORK/src"
    mkdir -p "$WORK/src"
    curl -fsSL --retry 3 --retry-delay 2 -o "$WORK/pack" "$url"
    case "$url" in
        *.zip)
            unzip -q "$WORK/pack" -d "$WORK/src"
            ;;
        *)
            if ! tar -xf "$WORK/pack" -C "$WORK/src"; then
                rm -rf "$WORK/src"
                mkdir -p "$WORK/src"
                case "$url" in
                    *.bz2)
                        if ! command -v python3 >/dev/null 2>&1; then
                            echo "tar could not open $url. Install bzip2, or provide python3." >&2
                            exit 1
                        fi
                        python3 -c 'import bz2,sys; open(sys.argv[2],"wb").write(bz2.decompress(open(sys.argv[1],"rb").read()))' "$WORK/pack" "$WORK/unpacked.tar"
                        tar -xf "$WORK/unpacked.tar" -C "$WORK/src"
                        rm -f "$WORK/unpacked.tar"
                        ;;
                    *)
                        echo "tar could not extract $url" >&2
                        exit 1
                        ;;
                esac
            fi
            ;;
    esac
    rm -f "$WORK/pack"
}

one_top() {
    label=$1
    ONE_TOP=""
    n=0
    for dir in "$WORK/src"/*; do
        if [ -d "$dir" ]; then
            n=$((n + 1))
            ONE_TOP=$dir
        fi
    done
    if [ "$n" -ne 1 ]; then
        echo "expected one directory in $label, found $n" >&2
        exit 1
    fi
}

link_name() {
    # link_name <exe-name> <link-name-without-dir>
    exe_name=$1
    link_name=$2
    if [ -e "$DEST/bin/$link_name" ]; then
        return 0
    fi
    if ! ln -s "$exe_name" "$DEST/bin/$link_name" 2>/dev/null; then
        printf '%s\n' '#!/bin/sh' "exec \"\$(dirname \"\$0\")/$exe_name\" \"\$@\"" > "$DEST/bin/$link_name"
        chmod 755 "$DEST/bin/$link_name"
    fi
}

unpack_project() {
    url=$1
    dest=$2
    marker=$3
    if [ -f "$dest/$marker" ]; then
        return 0
    fi
    fetch_extract "$url"
    one_top "$url"
    rm -rf "$dest"
    mkdir -p "$(dirname "$dest")"
    mv "$ONE_TOP" "$dest"
    if [ ! -f "$dest/$marker" ]; then
        echo "missing $dest/$marker" >&2
        exit 1
    fi
}

install_ninja() {
    if [ -e "$DEST/bin/ninja" ]; then
        return 0
    fi
    case "$OS" in
        linux) url=$NINJA_LINUX_URL ;;
        darwin) url=$NINJA_DARWIN_URL ;;
        windows) url=$NINJA_WINDOWS_URL ;;
    esac
    fetch_extract "$url"
    if [ -f "$WORK/src/ninja" ]; then
        cp -f "$WORK/src/ninja" "$DEST/bin/ninja"
        chmod 755 "$DEST/bin/ninja"
    elif [ -f "$WORK/src/ninja.exe" ]; then
        cp -f "$WORK/src/ninja.exe" "$DEST/bin/ninja.exe"
        chmod 755 "$DEST/bin/ninja.exe"
        link_name ninja.exe ninja
    else
        echo "ninja archive has no ninja binary" >&2
        exit 1
    fi
}

install_python() {
    case "$OS" in
        darwin)
            return 0
            ;;
        linux)
            if [ -x "$DEST/python/bin/python3" ]; then
                return 0
            fi
            fetch_extract "$PYTHON_LINUX_URL"
            if [ -x "$WORK/src/python/bin/python3" ]; then
                rm -rf "$DEST/python"
                mv "$WORK/src/python" "$DEST/python"
            else
                one_top "$PYTHON_LINUX_URL"
                if [ ! -x "$ONE_TOP/bin/python3" ]; then
                    echo "python archive has no bin/python3" >&2
                    exit 1
                fi
                rm -rf "$DEST/python"
                mv "$ONE_TOP" "$DEST/python"
            fi
            ;;
        windows)
            if [ -f "$DEST/python/python.exe" ]; then
                return 0
            fi
            echo "fetch $PYTHON_WINDOWS_URL"
            curl -fsSL --retry 3 --retry-delay 2 -o "$WORK/pack" "$PYTHON_WINDOWS_URL"
            rm -rf "$DEST/python"
            mkdir -p "$DEST/python"
            unzip -q "$WORK/pack" -d "$DEST/python"
            rm -f "$WORK/pack"
            if [ ! -f "$DEST/python/python.exe" ]; then
                echo "python embed zip has no python.exe" >&2
                exit 1
            fi
            ;;
    esac
}

install_xwin() {
    if [ "$OS" != windows ]; then
        return 0
    fi
    if [ -f "$DEST/xwin/xwin.exe" ]; then
        return 0
    fi
    fetch_extract "$XWIN_WINDOWS_URL"
    one_top "$XWIN_WINDOWS_URL"
    if [ ! -f "$ONE_TOP/xwin.exe" ]; then
        echo "xwin archive has no xwin.exe" >&2
        exit 1
    fi
    mkdir -p "$DEST/xwin"
    cp -f "$ONE_TOP/xwin.exe" "$DEST/xwin/xwin.exe"
    chmod 755 "$DEST/xwin/xwin.exe"
}

install_zydis() {
    dest="$DEST/third_party/zydis"
    if [ -f "$dest/CMakeLists.txt" ] && [ -f "$dest/dependencies/zycore/CMakeLists.txt" ]; then
        return 0
    fi
    echo "clone $ZYDIS_GIT_URL $ZYDIS_TAG"
    rm -rf "$dest"
    GIT_TERMINAL_PROMPT=0 git clone --depth 1 --recurse-submodules \
        --branch "$ZYDIS_TAG" "$ZYDIS_GIT_URL" "$dest"
    if [ ! -f "$dest/dependencies/zycore/CMakeLists.txt" ]; then
        echo "zydis clone is missing the zycore submodule" >&2
        exit 1
    fi
}

have_loader() {
    for lib in "$PREFIX/lib"/libvulkan.*; do
        if [ -e "$lib" ]; then
            return 0
        fi
    done
    return 1
}

build_cmake_src() {
    src=$1
    shift
    build="$WORK/build"
    rm -rf "$build"
    cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        "$@"
    cmake --build "$build" --parallel
    cmake --install "$build"
}

install_prefix() {
    if [ "$OS" = windows ]; then
        return 0
    fi

    if [ ! -f "$PREFIX/include/vulkan/vulkan.h" ]; then
        fetch_extract "$VULKAN_HEADERS_URL"
        one_top "$VULKAN_HEADERS_URL"
        build_cmake_src "$ONE_TOP" \
            -DVULKAN_HEADERS_ENABLE_MODULE=OFF \
            -DVULKAN_HEADERS_ENABLE_TESTS=OFF
    fi

    if ! have_loader; then
        fetch_extract "$VULKAN_LOADER_URL"
        one_top "$VULKAN_LOADER_URL"
        build_cmake_src "$ONE_TOP" \
            -DBUILD_TESTS=OFF \
            -DLOADER_CODEGEN=OFF
    fi
    if ! have_loader; then
        echo "vulkan loader was not installed into prefix/lib" >&2
        exit 1
    fi

    if [ ! -f "$PREFIX/lib/libz.a" ] || [ ! -f "$PREFIX/include/zlib.h" ]; then
        fetch_extract "$ZLIB_URL"
        one_top "$ZLIB_URL"
        build_cmake_src "$ONE_TOP" \
            -DZLIB_BUILD_SHARED=OFF \
            -DZLIB_BUILD_STATIC=ON \
            -DZLIB_BUILD_TESTING=OFF
    fi
    if [ ! -f "$PREFIX/lib/libz.a" ] || [ ! -f "$PREFIX/include/zlib.h" ]; then
        echo "zlib static library was not installed" >&2
        exit 1
    fi

    if [ ! -f "$PREFIX/lib/libglslang.a" ] \
        || [ ! -f "$PREFIX/lib/libSPIRV.a" ] \
        || [ ! -f "$PREFIX/lib/libglslang-default-resource-limits.a" ] \
        || [ ! -f "$PREFIX/include/glslang/Public/ShaderLang.h" ]; then
        fetch_extract "$GLSLANG_URL"
        one_top "$GLSLANG_URL"
        # ENABLE_OPT is off: the tag archive has no SPIRV-Tools submodule.
        # glslang 16 still installs MachineIndependent and GenericCodeGen stubs when static.
        build_cmake_src "$ONE_TOP" \
            -DBUILD_SHARED_LIBS=OFF \
            -DBUILD_EXTERNAL=OFF \
            -DENABLE_OPT=OFF \
            -DENABLE_HLSL=ON \
            -DENABLE_PCH=OFF \
            -DGLSLANG_TESTS=OFF \
            -DENABLE_GLSLANG_BINARIES=OFF
    fi
    if [ ! -f "$PREFIX/lib/libglslang.a" ] \
        || [ ! -f "$PREFIX/lib/libSPIRV.a" ] \
        || [ ! -f "$PREFIX/lib/libglslang-default-resource-limits.a" ]; then
        echo "glslang static libraries were not installed" >&2
        exit 1
    fi

    if [ "$OS" = darwin ]; then
        if [ ! -f "$PREFIX/lib/libusb-1.0.a" ] || [ ! -f "$PREFIX/include/libusb-1.0/libusb.h" ]; then
            fetch_extract "$LIBUSB_URL"
            one_top "$LIBUSB_URL"
            # libtool splits an install prefix on spaces. Ring Host.app has one,
            # so install into a temporary prefix and copy the static library.
            usb_stage=$(mktemp -d)
            (
                cd "$ONE_TOP"
                CFLAGS="-fPIC" ./configure --prefix="$usb_stage" --enable-static --disable-shared
                make -j"$JOBS"
                make install
            )
            mkdir -p "$PREFIX/lib" "$PREFIX/include"
            cp -a "$usb_stage/lib/libusb-1.0.a" "$PREFIX/lib/"
            cp -a "$usb_stage/include/libusb-1.0" "$PREFIX/include/"
            rm -rf "$usb_stage"
            rm -f "$PREFIX/lib/libusb-1.0.dylib"
        fi
        if [ ! -f "$PREFIX/lib/libusb-1.0.a" ] || [ ! -f "$PREFIX/include/libusb-1.0/libusb.h" ]; then
            echo "libusb static library was not installed" >&2
            exit 1
        fi

        icd="$PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json"
        mvk="$PREFIX/lib/libMoltenVK.dylib"
        if [ ! -f "$icd" ] || [ ! -f "$mvk" ]; then
            fetch_extract "$MOLTENVK_MACOS_URL"
            dylib="$WORK/src/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
            if [ ! -f "$dylib" ]; then
                echo "MoltenVK archive has no macOS dylib" >&2
                exit 1
            fi
            mkdir -p "$PREFIX/lib" "$(dirname "$icd")"
            cp -f "$dylib" "$mvk"
            # library_path is relative to the json file, not a Homebrew path.
            cat > "$icd" <<'EOF'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../lib/libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
EOF
        fi
    fi
}

install_llvm() {
    case "$OS" in
        darwin)
            return 0
            ;;
        linux)
            if [ ! -x "$DEST/llvm/bin/clang" ]; then
                fetch_extract "$LLVM_LINUX_URL"
                one_top "$LLVM_LINUX_URL"
                rm -rf "$DEST/llvm"
                mv "$ONE_TOP" "$DEST/llvm"
            fi
            if [ ! -x "$DEST/llvm/bin/clang" ] || [ ! -x "$DEST/llvm/bin/clang++" ]; then
                echo "llvm archive has no bin/clang" >&2
                exit 1
            fi
            ln -sfn ../llvm/bin/clang "$DEST/bin/clang"
            ln -sfn ../llvm/bin/clang++ "$DEST/bin/clang++"
            # $ORIGIN/../lib when clang is invoked from dest/bin, and the real tree under dest/llvm.
            if [ ! -e "$DEST/lib" ]; then
                ln -s llvm/lib "$DEST/lib"
            fi
            ;;
        windows)
            if [ ! -f "$DEST/llvm/bin/clang-cl.exe" ]; then
                fetch_extract "$LLVM_WINDOWS_URL"
                one_top "$LLVM_WINDOWS_URL"
                rm -rf "$DEST/llvm"
                mv "$ONE_TOP" "$DEST/llvm"
            fi
            for name in clang-cl.exe lld-link.exe llvm-lib.exe llvm-rc.exe llvm-mt.exe; do
                if [ ! -f "$DEST/llvm/bin/$name" ]; then
                    echo "llvm archive has no bin/$name" >&2
                    exit 1
                fi
                cp -f "$DEST/llvm/bin/$name" "$DEST/bin/$name"
            done
            for dll in "$DEST/llvm/bin/"*.dll; do
                if [ -f "$dll" ]; then
                    cp -f "$dll" "$DEST/bin/$(basename "$dll")"
                fi
            done
            if [ ! -e "$DEST/lib/clang" ]; then
                mkdir -p "$DEST/lib"
                if ! ln -s ../llvm/lib/clang "$DEST/lib/clang" 2>/dev/null; then
                    cp -a "$DEST/llvm/lib/clang" "$DEST/lib/clang"
                fi
            fi
            ;;
    esac
}

install_sdl() {
    unpack_project "$SDL_URL" "$DEST/third_party/SDL" CMakeLists.txt
}

install_cmake_files() {
    # Stage every cmake file except the bin/cmake entry point.
    # publish_cmake creates that entry last, so a partial run is not treated as done.
    if [ -e "$DEST/bin/cmake" ]; then
        return 0
    fi
    share_ready=0
    for dir in "$DEST/share"/cmake-*; do
        if [ -d "$dir" ]; then
            share_ready=1
        fi
    done
    if [ "$share_ready" -eq 1 ] && [ -e "$DEST/bin/.cmake-entry" -o -f "$DEST/bin/cmake.exe" ]; then
        return 0
    fi
    case "$OS" in
        linux) url=$CMAKE_LINUX_URL ;;
        darwin) url=$CMAKE_DARWIN_URL ;;
        windows) url=$CMAKE_WINDOWS_URL ;;
    esac
    fetch_extract "$url"
    one_top "$url"
    if [ ! -d "$ONE_TOP/bin" ] || [ ! -d "$ONE_TOP/share" ]; then
        echo "cmake archive has no bin and share" >&2
        exit 1
    fi
    mkdir -p "$DEST/bin" "$DEST/share"
    cp -a "$ONE_TOP/share/." "$DEST/share/"
    for file in "$ONE_TOP/bin"/*; do
        base=$(basename "$file")
        if [ "$base" = cmake ] || [ "$base" = cmake.exe ]; then
            continue
        fi
        if [ ! -e "$DEST/bin/$base" ]; then
            cp -a "$file" "$DEST/bin/$base"
        fi
    done
    if [ -f "$ONE_TOP/bin/cmake" ]; then
        cp -f "$ONE_TOP/bin/cmake" "$DEST/bin/.cmake-entry"
        chmod 755 "$DEST/bin/.cmake-entry"
    fi
    if [ -f "$ONE_TOP/bin/cmake.exe" ]; then
        cp -f "$ONE_TOP/bin/cmake.exe" "$DEST/bin/cmake.exe"
        chmod 755 "$DEST/bin/cmake.exe"
    fi
}

publish_cmake() {
    if [ -e "$DEST/bin/cmake" ]; then
        return 0
    fi
    if [ -f "$DEST/bin/.cmake-entry" ]; then
        mv "$DEST/bin/.cmake-entry" "$DEST/bin/cmake"
        chmod 755 "$DEST/bin/cmake"
        return 0
    fi
    if [ -f "$DEST/bin/cmake.exe" ]; then
        link_name cmake.exe cmake
        return 0
    fi
    echo "cmake entry was not staged" >&2
    exit 1
}

must() {
    if [ ! -e "$1" ]; then
        echo "toolchain is missing $1" >&2
        exit 1
    fi
}

finish_check() {
    must "$DEST/bin/ninja"
    must "$DEST/third_party/imgui/imgui.cpp"
    must "$DEST/third_party/zydis/CMakeLists.txt"
    must "$DEST/third_party/zydis/dependencies/zycore/CMakeLists.txt"
    must "$DEST/third_party/tomlplusplus/CMakeLists.txt"
    case "$OS" in
        linux)
            must "$DEST/bin/clang"
            must "$DEST/bin/clang++"
            must "$DEST/llvm/bin/clang"
            must "$DEST/python/bin/python3"
            must "$PREFIX/include/vulkan/vulkan.h"
            must "$PREFIX/lib/libz.a"
            must "$PREFIX/include/zlib.h"
            must "$PREFIX/lib/libglslang.a"
            must "$PREFIX/lib/libSPIRV.a"
            must "$PREFIX/lib/libglslang-default-resource-limits.a"
            must "$PREFIX/include/glslang/Public/ShaderLang.h"
            if ! have_loader; then
                echo "toolchain is missing the vulkan loader" >&2
                exit 1
            fi
            ;;
        darwin)
            must "$PREFIX/include/vulkan/vulkan.h"
            must "$PREFIX/lib/libz.a"
            must "$PREFIX/include/zlib.h"
            must "$PREFIX/lib/libglslang.a"
            must "$PREFIX/lib/libSPIRV.a"
            must "$PREFIX/lib/libglslang-default-resource-limits.a"
            must "$PREFIX/include/glslang/Public/ShaderLang.h"
            must "$PREFIX/lib/libusb-1.0.a"
            must "$PREFIX/include/libusb-1.0/libusb.h"
            must "$PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json"
            must "$PREFIX/lib/libMoltenVK.dylib"
            if ! have_loader; then
                echo "toolchain is missing the vulkan loader" >&2
                exit 1
            fi
            ;;
        windows)
            must "$DEST/bin/clang-cl.exe"
            must "$DEST/bin/lld-link.exe"
            must "$DEST/bin/llvm-lib.exe"
            must "$DEST/bin/llvm-rc.exe"
            must "$DEST/bin/llvm-mt.exe"
            must "$DEST/python/python.exe"
            must "$DEST/xwin/xwin.exe"
            ;;
    esac
}

install_ninja
unpack_project "$IMGUI_URL" "$DEST/third_party/imgui" imgui.cpp
unpack_project "$TOMLPLUSPLUS_URL" "$DEST/third_party/tomlplusplus" CMakeLists.txt
install_zydis
install_python
install_xwin
install_prefix
install_llvm
install_cmake_files
finish_check
install_sdl
publish_cmake

echo "toolchain $DEST"
