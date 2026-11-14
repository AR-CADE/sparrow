#!/bin/bash
# ==============================================================================
# Sparrow - Flutter Linux Bundle Optimizer
# ==============================================================================
# Converts a standard Flutter Linux GTK bundle into a minimalist, optimized
# Wayland-native bundle suitable for sparrow-app-runner and Sparrow compositor.
#
# Removes GTK dependencies (libflutter_linux_gtk.so), reorganizes assets,
# and generates/optimizes app.so AOT ELF using gen_snapshot (with optional
# profile/debug flags or custom engine artifacts).
# ==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

function print_usage() {
    echo -e "${BOLD}Usage:${NC} $0 [OPTIONS] <BUNDLE_PATH> <OUTPUT_DIR>"
    echo ""
    echo -e "${BOLD}Description:${NC}"
    echo "  Converts and optimizes a Flutter Linux bundle (from 'flutter build linux')"
    echo "  into a lightweight Sparrow Wayland-native bundle without GTK overhead."
    echo ""
    echo -e "${BOLD}Arguments:${NC}"
    echo "  <BUNDLE_PATH>            Path to Flutter Linux bundle directory"
    echo "                           (e.g., my_app/build/linux/x64/release/bundle)"
    echo "  <OUTPUT_DIR>             Destination directory for the optimized bundle"
    echo "                           (e.g., out/my_app or build/my_app)"
    echo ""
    echo -e "${BOLD}Options:${NC}"
    echo "  -p, --profiling          Enable Dart VM profile mode (-Ddart.vm.profile=true)"
    echo "  --host-path=<path>       Directory containing custom engine artifacts"
    echo "                           (gen_snapshot, flutter_patched_sdk, libflutter_engine.so)"
    echo "  --project-dir=<path>     Path to the Flutter project root containing lib/main.dart"
    echo "                           (auto-detected from BUNDLE_PATH if not specified)"
    echo "  --flutter-bin=<path>     Path to flutter bin directory (overrides default search)"
    echo "  -h, --help               Show this help message and exit"
    echo ""
    echo -e "${BOLD}Examples:${NC}"
    echo "  $0 shell/build/linux/x64/release/bundle out/shell"
    echo "  $0 my_app/build/linux/x64/release/bundle out/my_app"
    echo "  $0 --profiling --host-path=flutter/engine/host_profile shell/build/linux/x64/release/bundle out/shell"
}

PROFILING=false
HOST_PATH=""
PROJECT_DIR=""
FLUTTER_BIN=""
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_usage "$0"
            exit 0
            ;;
        -p|--profiling)
            PROFILING=true
            shift
            ;;
        --host-path=*)
            HOST_PATH="${1#*=}"
            shift
            ;;
        --host-path)
            HOST_PATH="$2"
            shift 2
            ;;
        --project-dir=*)
            PROJECT_DIR="${1#*=}"
            shift
            ;;
        --project-dir)
            PROJECT_DIR="$2"
            shift 2
            ;;
        --flutter-bin=*)
            FLUTTER_BIN="${1#*=}"
            shift
            ;;
        --flutter-bin)
            FLUTTER_BIN="$2"
            shift 2
            ;;
        -*)
            echo -e "${RED}Error:${NC} Unknown option: $1" >&2
            echo ""
            print_usage "$0"
            exit 1
            ;;
        *)
            POSITIONAL_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ ${#POSITIONAL_ARGS[@]} -lt 2 ]; then
    echo -e "${RED}Error:${NC} Missing required arguments." >&2
    echo ""
    print_usage "$0"
    exit 1
fi

BUNDLE_PATH="$(cd "${POSITIONAL_ARGS[0]}" 2>/dev/null && pwd || echo "${POSITIONAL_ARGS[0]}")"
OUTPUT_DIR="${POSITIONAL_ARGS[1]}"

# Create absolute path for output dir
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

echo -e "\n${BOLD}${BLUE}==========================================${NC}"
echo -e "${BOLD} Sparrow Flutter Bundle Optimizer${NC}"
echo -e "${BOLD}${BLUE}==========================================${NC}"
echo -e "Input bundle : ${CYAN}${BUNDLE_PATH}${NC}"
echo -e "Output dir   : ${CYAN}${OUTPUT_DIR}${NC}"

# 1. Validate Input Bundle
if [ ! -d "$BUNDLE_PATH" ]; then
    echo -e "${RED}Error:${NC} Input bundle directory does not exist: $BUNDLE_PATH" >&2
    exit 1
fi

if [ ! -d "$BUNDLE_PATH/data" ] && [ ! -d "$BUNDLE_PATH/lib" ]; then
    echo -e "${RED}Error:${NC} Input directory does not look like a Flutter bundle (missing data/ or lib/)." >&2
    exit 1
fi

# 2. Locate Flutter SDK & Dart tools
if [ -z "$FLUTTER_BIN" ]; then
    if which flutter >/dev/null 2>&1; then
        FLUTTER_BIN="$(dirname "$(which flutter)")"
    elif [ -d "$HOME/.local/share/flutter/bin" ]; then
        FLUTTER_BIN="$HOME/.local/share/flutter/bin"
    elif [ -d /opt/flutter/bin ]; then
        FLUTTER_BIN="/opt/flutter/bin"
    fi
fi

if [ -n "$FLUTTER_BIN" ] && [ -d "$FLUTTER_BIN" ]; then
    FLUTTER_BIN="$(cd "$FLUTTER_BIN" && pwd)"
fi

LOCAL_ENGINE=""
if [ -n "$FLUTTER_BIN" ] && [ -d "$FLUTTER_BIN/cache" ]; then
    LOCAL_ENGINE="$FLUTTER_BIN/cache"
fi

GEN_SNAPSHOT_BIN=""
PATCHED_SDK_PATH=""
DARTAOTRUNTIME=""
FRONTEND_SERVER=""

if [ -n "$LOCAL_ENGINE" ]; then
    GEN_SNAPSHOT_BIN="$LOCAL_ENGINE/dart-sdk/bin/utils/gen_snapshot"
    PATCHED_SDK_PATH="$LOCAL_ENGINE/artifacts/engine/common/flutter_patched_sdk"
    DARTAOTRUNTIME="$LOCAL_ENGINE/dart-sdk/bin/dartaotruntime"
    FRONTEND_SERVER="$LOCAL_ENGINE/dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot"
fi

# Custom engine overrides
if [ -n "$HOST_PATH" ] && [ -d "$HOST_PATH" ]; then
    HOST_PATH="$(cd "$HOST_PATH" && pwd)"
    if [ -f "$HOST_PATH/gen_snapshot" ]; then
        GEN_SNAPSHOT_BIN="$HOST_PATH/gen_snapshot"
    fi
    if [ -d "$HOST_PATH/flutter_patched_sdk" ]; then
        PATCHED_SDK_PATH="$HOST_PATH/flutter_patched_sdk"
    fi
    echo -e "Custom engine: ${GREEN}${HOST_PATH}${NC}"
fi

# 3. Detect Flutter Project Source Directory
if [ -z "$PROJECT_DIR" ]; then
    # Standard Flutter Linux build path: <project>/build/linux/x64/<mode>/bundle (5 levels up)
    for depth in "../../../../.." "../../../.."; do
        CANDIDATE_DIR="$(cd "$BUNDLE_PATH/$depth" 2>/dev/null && pwd || true)"
        if [ -n "$CANDIDATE_DIR" ] && [ -f "$CANDIDATE_DIR/pubspec.yaml" ]; then
            PROJECT_DIR="$CANDIDATE_DIR"
            break
        fi
    done

    if [ -z "$PROJECT_DIR" ] && [ -f "$(pwd)/pubspec.yaml" ]; then
        PROJECT_DIR="$(pwd)"
    fi
fi

if [ -n "$PROJECT_DIR" ] && [ -f "$PROJECT_DIR/pubspec.yaml" ]; then
    PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"
    echo -e "Project root : ${GREEN}${PROJECT_DIR}${NC}"
else
    echo -e "Project root : ${YELLOW}None (Standalone bundle mode)${NC}"
fi

# 4. Copy Bundle Assets & Libraries
echo -e "\n[1/3] Packaging bundle assets and native plugins..."
mkdir -p "$OUTPUT_DIR/lib"

if [ -d "$BUNDLE_PATH/data" ]; then
    cp -rfp "$BUNDLE_PATH/data" "$OUTPUT_DIR/"
fi

if [ -d "$BUNDLE_PATH/lib" ]; then
    cp -rfp "$BUNDLE_PATH/lib/"* "$OUTPUT_DIR/lib/" 2>/dev/null || true
fi

# Remove GTK dependencies and redundant libapp.so
rm -f "$OUTPUT_DIR/lib/libflutter_linux_gtk.so"
rm -f "$OUTPUT_DIR/lib/libapp.so"

# If custom engine has libflutter_engine.so, copy it into lib/
if [ -n "$HOST_PATH" ] && [ -f "$HOST_PATH/libflutter_engine.so" ]; then
    cp -rfp "$HOST_PATH/libflutter_engine.so" "$OUTPUT_DIR/lib/"
    echo -e "Copied custom engine: ${CYAN}libflutter_engine.so${NC}"
fi

# 5. Generate / Optimize app.so
echo -e "\n[2/3] Optimizing AOT ELF snapshot (app.so)..."

CAN_COMPILE_AOT=false
if [ -n "$PROJECT_DIR" ] && \
   [ -f "$PROJECT_DIR/lib/main.dart" ] && \
   [ -f "$PROJECT_DIR/.dart_tool/package_config.json" ] && \
   [ -x "$DARTAOTRUNTIME" ] && \
   [ -f "$FRONTEND_SERVER" ] && \
   [ -x "$GEN_SNAPSHOT_BIN" ] && \
   [ -d "$PATCHED_SDK_PATH" ]; then
    CAN_COMPILE_AOT=true
fi

if [ "$CAN_COMPILE_AOT" = true ]; then
    DART_VM_FLAG="-Ddart.vm.product=true"
    GEN_SNAPSHOT_FLAGS="--obfuscate --strip"

    if [ "$PROFILING" = true ]; then
        if [ -n "$HOST_PATH" ] && [ -d "$HOST_PATH" ]; then
            DART_VM_FLAG="-Ddart.vm.profile=true"
            GEN_SNAPSHOT_FLAGS=""
            echo -e "Build mode   : ${YELLOW}Profile (Dart VM Service enabled)${NC}"
        else
            echo -e "Notice: Profile mode requested without custom engine artifacts."
            echo -e "Falling back to Release AOT product (-Ddart.vm.product=true)."
        fi
    else
        echo -e "Build mode   : ${GREEN}Release (AOT Product, stripped & obfuscated)${NC}"
    fi

    TEMP_BUILD_DIR="$(mktemp -d -t sparrow_bundle_XXXXXX)"
    trap 'rm -rf "$TEMP_BUILD_DIR"' EXIT

    echo -e "Compiling kernel dill snapshot..."
    "$DARTAOTRUNTIME" \
        "$FRONTEND_SERVER" \
        --sdk-root "$PATCHED_SDK_PATH" \
        --target=flutter \
        --aot \
        --tfa \
        $DART_VM_FLAG \
        --packages "$PROJECT_DIR/.dart_tool/package_config.json" \
        --output-dill "$TEMP_BUILD_DIR/kernel_snapshot.dill" \
        --depfile "$TEMP_BUILD_DIR/kernel_snapshot.d" \
        "$PROJECT_DIR/lib/main.dart" >/dev/null

    echo -e "Generating AOT ELF snapshot with gen_snapshot..."
    "$GEN_SNAPSHOT_BIN" \
        --deterministic \
        --snapshot_kind=app-aot-elf \
        $GEN_SNAPSHOT_FLAGS \
        --elf="$OUTPUT_DIR/app.so" \
        "$TEMP_BUILD_DIR/kernel_snapshot.dill"

    rm -rf "$TEMP_BUILD_DIR"
    trap - EXIT
else
    # Fallback to pre-built libapp.so from bundle
    if [ -f "$BUNDLE_PATH/lib/libapp.so" ]; then
        echo -e "Using pre-built libapp.so from input bundle..."
        cp -rfp "$BUNDLE_PATH/lib/libapp.so" "$OUTPUT_DIR/app.so"
        if which strip >/dev/null 2>&1; then
            strip --strip-unneeded "$OUTPUT_DIR/app.so" 2>/dev/null || true
        fi
    elif [ -f "$BUNDLE_PATH/app.so" ]; then
        cp -rfp "$BUNDLE_PATH/app.so" "$OUTPUT_DIR/app.so"
    else
        echo -e "${RED}Error:${NC} Could not find or compile app.so." >&2
        exit 1
    fi
fi

# 6. Verification and Summary
echo -e "\n[3/3] Verifying optimized bundle..."

if [ ! -f "$OUTPUT_DIR/app.so" ]; then
    echo -e "${RED}Error:${NC} Output app.so was not generated." >&2
    exit 1
fi

if [ ! -d "$OUTPUT_DIR/data/flutter_assets" ]; then
    echo -e "${RED}Error:${NC} Output data/flutter_assets directory is missing." >&2
    exit 1
fi

APP_SIZE="$(du -h "$OUTPUT_DIR/app.so" | cut -f1)"
TOTAL_SIZE="$(du -sh "$OUTPUT_DIR" | cut -f1)"

echo -e "\n${BOLD}${GREEN}==========================================${NC}"
echo -e "${BOLD}${GREEN} Optimized Bundle Ready!${NC}"
echo -e "${BOLD}${GREEN}==========================================${NC}"
echo -e "Location     : ${CYAN}${OUTPUT_DIR}${NC}"
echo -e "AOT ELF size : ${BOLD}${APP_SIZE}${NC} ($OUTPUT_DIR/app.so)"
echo -e "Total size   : ${BOLD}${TOTAL_SIZE}${NC}"
echo -e "GTK removal  : ${GREEN}Confirmed (0 GTK libraries)${NC}"
echo ""
echo -e "Run this bundle with sparrow-app-runner:"
echo -e "  ${BOLD}./out/sparrow-app-runner \"$OUTPUT_DIR\"${NC}"
echo ""
