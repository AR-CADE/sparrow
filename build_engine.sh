#!/bin/bash
set -e

RUNTIME_MODE="release"
ENGINE_VERSION="${ENGINE_VERSION:-master}"

# Parse command line options
for arg in "$@"; do
    case "$arg" in
        profile|--profile)
            RUNTIME_MODE="profile"
            ;;
        debug|--debug)
            RUNTIME_MODE="debug"
            ;;
        release|--release)
            RUNTIME_MODE="release"
            ;;
        --version=*|-v=*)
            ENGINE_VERSION="${arg#*=}"
            ;;
        *)
            # If argument looks like a version number (x.y.z)
            if [[ "$arg" =~ ^[0-9]+\.[0-9]+ ]]; then
                ENGINE_VERSION="$arg"
            fi
            ;;
    esac
done

echo "Building Flutter Engine..."
echo "  Version      : $ENGINE_VERSION"
echo "  Runtime Mode : $RUNTIME_MODE"
echo ""

#mkdir -p build

rm -rf engine
mkdir -p engine
cd engine

if [ ! -d "depot_tools" ]; then
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi
export PATH="$PWD/depot_tools:$PATH"

cat <<EOF | tee .gclient
solutions = [
    {
	"managed": False,
	"name": ".",
	"url": "https://github.com/flutter/flutter.git",
	"custom_deps": {},
	"deps_file": "DEPS",
	"safesync_url": "",
	"custom_vars": {
	    "download_android_deps": False,
	    "download_windows_deps": False,
    },
},
]
EOF

gclient sync -D --revision "$ENGINE_VERSION" --no-history 

TOOLS_PATH="./engine/src/flutter/tools"

$TOOLS_PATH/gn --runtime-mode "$RUNTIME_MODE" \
--enable-fontconfig \
--no-goma \
--disable-desktop-embeddings \
--no-build-embedder-examples \
--no-enable-unittests \
--embedder-for-target \
--stripped \
--optimize-for-size \
--ccache \
--lto \
--clang

OUT_DIR="engine/src/out/host_${RUNTIME_MODE}"
ninja -C "$OUT_DIR"

echo ""
echo "Flutter Engine build finished successfully:"
echo "  Artifacts located in: $PWD/$OUT_DIR"
echo "  libflutter_engine.so: $PWD/$OUT_DIR/libflutter_engine.so"

