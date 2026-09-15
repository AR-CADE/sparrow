#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function clean() {
    echo -e '\nCleaning sparrow...\n'
    rm -rf build
    rm -rf subprojects/packagecache
    rm -rf subprojects/.wraplock
    rm -rf subprojects/flutter_embedder/*.so
    rm -rf subprojects/flutter_embedder/*.zip
    rm -rf subprojects/flutter_embedder/*.h
    rm -rf subprojects/flutter_embedder/*.md
    rm -rf subprojects/wlroots
    rm -rf subprojects/wleird
    rm -rf subprojects/compositor-killer
    rm -rf build-ck
    rm -rf out
    rm -rf sparrow.prof*
    rm -rf platform/pigeon/compositor/cpp/*.g.*
    rm -rf platform/pigeon/compositor/gobject/*.g.*
    rm -rf platform/pigeon/compositor/pigeon_compositor/lib/src/*.g.dart
    rm -rf platform/pigeon/runner/cpp/*.g.*
    rm -rf platform/pigeon/runner/gobject/*.g.*
    rm -rf platform/pigeon/runner/pigeon_runner/lib/src/*.g.dart
}

function show_help() {
    echo "Usage: ./build.sh [OPTIONS...]"
    echo ""
    echo "Build Modes (choose one base mode, default is release):"
    echo "  release (default)   Optimized build, automated 3-stage PGO pipeline"
    echo "  optimize            Optimized build with LTO (uses PGO if sparrow.profdata exists)"
    echo "  debug               Debug build with symbols, no optimizations"
    echo "  profile             Flutter profile build with Dart VM service & DevTools enabled"
    echo "  pgo-generate        Instrumented build for PGO profile data collection (-fprofile-instr-generate)"
    echo "  asan                Debug build with AddressSanitizer (ASan)"
    echo "  tsan                Debug build with ThreadSanitizer (TSan)"
    echo "  ubsan               Debug build with UndefinedBehaviorSanitizer (UBSan)"
    echo "  valgrind            Debug build with Valgrind (Memory error detector)"
    echo ""
    echo "Features & Modifiers (can be accumulated/combined):"
    echo "  vulkan              Enable Vulkan rendering backend (-Denable_vulkan=true)"
    echo "  no-vulkan           Disable Vulkan backend (-Denable_vulkan=false)"
    echo "  impeller            Enable Impeller rendering backend (-Denable_impeller=true)"
    echo "  no-impeller         Disable Impeller backend (-Denable_impeller=false)"
    echo "  pgo / no-pgo        Force enable / disable PGO in optimize mode"
    echo "  dmabuf              Enable direct DMA-buf (-Denable_dmabuf=true)"
    echo "  no-dmabuf           Disable direct DMA-buf (-Denable_dmabuf=false)"
    echo "  damage-history      Enable damage history tracking (-Denable_damage_history=true)"
    echo "  no-damage-history   Disable damage history tracking (-Denable_damage_history=false)"
    echo "  pigeon              Regenerate Pigeon C++ and Dart message bindings"
    echo "  tidy / clang-tidy   Run Clang-Tidy static analysis on C++ codebase"
    echo "  tidy-fix            Run Clang-Tidy with automatic fix application"
    echo "  server / no-client  Build only the C++ server binary"
    echo "  client / no-server  Build only the Flutter client shell"
    echo "  --host-path=<path>  Path to custom engine artifacts (e.g. flutter/engine/host_profile)"
    echo "  clean               Remove build/ and out/ directories"
    echo ""
    echo "Examples:"
    echo "  ./build.sh                               # Release (Automated 3-stage PGO pipeline)"
    echo "  ./build.sh release                       # Release (Automated 3-stage PGO pipeline)"
    echo "  ./build.sh optimize                      # Direct optimized build (LTO + PGO if sparrow.profdata exists)"
    echo "  ./build.sh optimize server               # Direct optimized build of C++ server only"
    echo "  ./build.sh pgo-generate                  # Compile instrumented server for PGO profiling"
    echo "  ./build.sh profile --host-path=flutter/engine/host_profile # Profile with Dart VM Service"
    echo "  ./build.sh no-impeller                   # Release with Skia (Skia + DMA-BUF + LTO)"
    echo "  ./build.sh vulkan                        # Release with Vulkan (Vulkan + Impeller + LTO)"
    echo "  ./build.sh asan                          # ASan debug"
    echo "  ./build.sh tsan                          # TSan debug"
    echo "  ./build.sh ubsan                         # UBSan debug"
    echo "  ./build.sh valgrind                      # Valgrind debug"
    echo "  ./build.sh debug server                  # Debug C++ server only"
    echo "  ./build.sh client                        # Rebuild Flutter client shell only"
    echo "  ./build.sh pigeon                        # Regenerate Pigeon message bindings"
    echo "  ./build.sh tidy                          # Run Clang-Tidy static analysis"
    echo "  ./build.sh tidy-fix                      # Run Clang-Tidy with auto-fix"
}

# --- Default Configuration ---
BUILD_TYPE="release"
ENABLE_IMPELLER=true
ENABLE_VULKAN=false
ENABLE_DAMAGE_HISTORY=true
ENABLE_TRACE=false
PGO=true
PGO_INSTRUMENT=false
PROFILING=false
ASAN=false
TSAN=false
UBSAN=false
VALGRIND=false
DISABLE_OPTS=false
LTO=true
DMABUF=true
SKIP_CLIENT=false
SKIP_SERVER=false
RUN_PIGEON=false
HOST_PATH=""
EXTRA_MESON_ARGS=()
FORCE_REGENERATE_PIGEON=false

# Direct delegation for automated PGO pipeline (release mode)
DO_RELEASE_PIPELINE=false
if [ $# -eq 0 ]; then
    DO_RELEASE_PIPELINE=true
fi
for arg in "$@"; do
    case "$arg" in
        release|--release)
            DO_RELEASE_PIPELINE=true
            ;;
        optimize|--optimize|debug|--debug|asan|--asan|tsan|--tsan|ubsan|--ubsan|valgrind|--valgrind|pgo-generate|--pgo-generate|pgo-instrument|--pgo-instrument|pgo-train|--pgo-train|profile|--profile|profiling|clean|help|-h|--help|client|server|pigeon|--pigeon|tidy|--tidy|clang-tidy|--clang-tidy|tidy-fix|--tidy-fix|clang-tidy-fix|--clang-tidy-fix)
            DO_RELEASE_PIPELINE=false
            break
            ;;
    esac
done

if [ "$DO_RELEASE_PIPELINE" = true ]; then
    PASSTHROUGH_ARGS=()
    for arg in "$@"; do
        if [ "$arg" != "release" ] && [ "$arg" != "--release" ]; then
            PASSTHROUGH_ARGS+=("$arg")
        fi
    done
    exec "${SCRIPT_DIR}/tools/bot/pgo_optimize.sh" "${PASSTHROUGH_ARGS[@]}"
fi

# --- Parse Arguments ---
for arg in "$@"; do
    case "$arg" in
        clean)
            clean
            exit 0
            ;;
        tidy|--tidy|clang-tidy|--clang-tidy)
            echo -e "\n=========================================="
            echo " Running Clang-Tidy Static Analysis"
            echo -e "==========================================\n"
            ninja -C build clang-tidy
            exit 0
            ;;
        tidy-fix|--tidy-fix|clang-tidy-fix|--clang-tidy-fix)
            echo -e "\n=========================================="
            echo " Running Clang-Tidy with Automatic Fixes"
            echo -e "==========================================\n"
            ninja -C build clang-tidy-fix
            exit 0
            ;;
        help|-h|--help)
            show_help
            exit 0
            ;;
        --host-path=*|-H=*)
            HOST_PATH="${arg#*=}"
            ;;
        debug|--debug)
            BUILD_TYPE="debug"
            DISABLE_OPTS=true
            LTO=false
            PGO=false
            PGO_INSTRUMENT=false
            ;;
        optimize|--optimize|release|--release)
            BUILD_TYPE="release"
            PGO_INSTRUMENT=false
            ;;
        pgo|--pgo)
            PGO=true
            PGO_INSTRUMENT=false
            ;;
        no-pgo|--no-pgo)
            PGO=false
            ;;
        pgo-generate|--pgo-generate|pgo-instrument|--pgo-instrument|pgo-train|--pgo-train)
            BUILD_TYPE="release"
            PGO=false
            PGO_INSTRUMENT=true
            ;;
        profile|--profile)
            BUILD_TYPE="release"
            PROFILING=true
            PGO=false
            PGO_INSTRUMENT=false
            ;;
        profiling|-Dprofiling=true)
            BUILD_TYPE="release"
            PGO=false
            PGO_INSTRUMENT=true
            ;;
        asan|--asan)
            BUILD_TYPE="debug"
            ASAN=true
            DISABLE_OPTS=true
            LTO=false
            PGO=false
            ;;
        tsan|--tsan)
            BUILD_TYPE="debug"
            TSAN=true
            DISABLE_OPTS=true
            LTO=false
            PGO=false
            ;;
        ubsan|--ubsan)
            BUILD_TYPE="debug"
            UBSAN=true
            DISABLE_OPTS=true
            LTO=false
            PGO=false
            ;;
        valgrind|--valgrind)
            BUILD_TYPE="debug"
            VALGRIND=true
            DISABLE_OPTS=true
            LTO=false
            PGO=false
            ;;
        vulkan|--vulkan|-Denable_vulkan=true)
            ENABLE_VULKAN=true
            ENABLE_IMPELLER=true
            ;;
        no-vulkan|--no-vulkan|-Denable_vulkan=false)
            ENABLE_VULKAN=false
            ;;
        impeller|--impeller|-Denable_impeller=true)
            ENABLE_IMPELLER=true
            ;;
        no-impeller|--no-impeller|-Denable_impeller=false)
            ENABLE_IMPELLER=false
            ;;
        dmabuf|--dmabuf|-Denable_dmabuf=true)
            DMABUF=true
            ;;
        no-dmabuf|--no-dmabuf|-Denable_dmabuf=false)
            DMABUF=false
            ;;
        damage-history|--damage-history|-Denable_damage_history=true)
            ENABLE_DAMAGE_HISTORY=true
            ;;
        no-damage-history|--no-damage-history|-Denable_damage_history=false)
            ENABLE_DAMAGE_HISTORY=false
            ;;
        trace|--trace|gpu-trace|--gpu-trace|-Denable_trace=true)
            ENABLE_TRACE=true
            ;;
        no-trace|--no-trace|-Denable_trace=false)
            ENABLE_TRACE=false
            ;;
        server-only|--server-only|server|--server|no-client|--no-client)
            SKIP_CLIENT=true
            ;;
        client-only|--client-only|client|--client|no-server|--no-server)
            SKIP_SERVER=true
            ;;
        pigeon|--pigeon)
            RUN_PIGEON=true
            ;;
        force-pigeon|--force-pigeon)
            RUN_PIGEON=true
            FORCE_REGENERATE_PIGEON=true
            ;;
        -D*|--*)
            EXTRA_MESON_ARGS+=("$arg")
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Run './build.sh --help' to see all available options."
            exit 1
            ;;
    esac
done

# --- Resolve Engine & PGO Paths ---
if [ -z "$HOST_PATH" ]; then
    if [ "$PROFILING" = true ] && [ -d "flutter/engine/host_profile" ]; then
        HOST_PATH="flutter/engine/host_profile"
    elif [ "$PROFILING" = false ] && [ -d "flutter/engine/host_release" ]; then
        HOST_PATH="flutter/engine/host_release"
    fi
fi

if [ "$PGO" = true ]; then
    if [ -f "sparrow.profdata" ]; then
        echo -e "\033[0;32m[PGO] Profile dataset found: sparrow.profdata ($(du -h sparrow.profdata | cut -f1)) - PGO enabled (-fprofile-instr-use)\033[0m"
        NEWER_SRC=$(find src -type f \( -name "*.cpp" -o -name "*.hpp" \) -newer sparrow.profdata 2>/dev/null | head -n 3)
        if [ -n "$NEWER_SRC" ]; then
            echo -e "\033[1;33m[PGO ALERT] Source files modified since sparrow.profdata was generated!\033[0m"
            echo -e "\033[1;33m            Profile data may contain obsolete functions (-Wprofile-instr-out-of-date).\033[0m"
            echo -e "\033[1;33m            Run ./build.sh release to retrain and regenerate sparrow.profdata.\033[0m"
        fi
    else
        echo -e "\033[1;33m[PGO] Notice: sparrow.profdata not found. Building release without PGO.\033[0m"
        echo -e "\033[1;33m      To run the automated PGO pipeline, run: ./build.sh release\033[0m\n"
        PGO=false
    fi
elif [ "$PGO_INSTRUMENT" = true ]; then
    echo -e "\033[0;36m[PGO] PGO Instrumentation active (-fprofile-instr-generate)\033[0m\n"
fi

# --- Construct Meson Options ---
MESON_ARGS=("--buildtype=$BUILD_TYPE")
MESON_ARGS+=("-Denable_impeller=$ENABLE_IMPELLER")
MESON_ARGS+=("-Denable_vulkan=$ENABLE_VULKAN")
MESON_ARGS+=("-Dpgo=$PGO")
MESON_ARGS+=("-Dprofiling=$PGO_INSTRUMENT")
MESON_ARGS+=("-Dasan=$ASAN")
MESON_ARGS+=("-Dtsan=$TSAN")
MESON_ARGS+=("-Dubsan=$UBSAN")
MESON_ARGS+=("-Dvalgrind=$VALGRIND")
MESON_ARGS+=("-Ddisable_optimizations=$DISABLE_OPTS")
MESON_ARGS+=("-Dlto=$LTO")
MESON_ARGS+=("-Denable_dmabuf=$DMABUF")
MESON_ARGS+=("-Denable_damage_history=$ENABLE_DAMAGE_HISTORY")
MESON_ARGS+=("-Denable_trace=$ENABLE_TRACE")

if [ ${#EXTRA_MESON_ARGS[@]} -gt 0 ]; then
    MESON_ARGS+=("${EXTRA_MESON_ARGS[@]}")
fi

function build_server() {
    if [ "$ENABLE_TRACE" = "true" ]; then
        if [ ! -f "subprojects/perfetto/perfetto.h" ] || [ ! -f "subprojects/perfetto/perfetto.cc" ]; then
            echo -e "\033[0;36m[TRACE] Downloading Perfetto C++ SDK (v48.1)...\033[0m"
            mkdir -p subprojects/perfetto
            curl -sSL https://github.com/google/perfetto/releases/download/v48.1/perfetto-sdk-v48.1.tar.gz | tar -xz -C subprojects/perfetto/ perfetto.h perfetto.cc
        fi
    fi

    echo -e "\n=========================================="
    echo " Configuring Sparrow Server"
    echo " Options: ${MESON_ARGS[*]}"
    echo -e "==========================================\n"

    env CC=clang CXX=clang++ \
    CXXFLAGS="-stdlib=libstdc++" \
    LDFLAGS="-fuse-ld=lld -stdlib=libstdc++" \
    meson setup build "${MESON_ARGS[@]}" --reconfigure 2>/dev/null || \
    env CC=clang CXX=clang++ \
    CXXFLAGS="-stdlib=libstdc++" \
    LDFLAGS="-fuse-ld=lld -stdlib=libstdc++" \
    meson setup build "${MESON_ARGS[@]}"

    echo -e '\nBuilding server...\n'
    ninja -C build
}

function build_client() {
    echo -e '\n=========================================='
    echo " Building Flutter Shell"
    echo -e "==========================================\n"

    mkdir -p build
    rm -rf build/shell
    mkdir -p build/shell/lib

    if [ -z "$LOCAL_BIN" ]; then
        if which flutter >/dev/null 2>&1; then
            LOCAL_BIN="$(dirname "$(which flutter)")"
        elif [ -d "$HOME/.local/share/flutter/bin" ]; then
            LOCAL_BIN="$HOME/.local/share/flutter/bin"
        elif [ -d /opt/flutter/bin ]; then
            LOCAL_BIN="/opt/flutter/bin"
        else
            LOCAL_BIN=~/.local/share/flutter/bin
        fi
    fi

    cd compositor_dart
    $LOCAL_BIN/flutter clean
    $LOCAL_BIN/flutter pub get
    $LOCAL_BIN/flutter pub upgrade
    $LOCAL_BIN/dart run build_runner build -r
    cd ..

    cd shell
    $LOCAL_BIN/flutter clean
    $LOCAL_BIN/flutter pub get
    $LOCAL_BIN/flutter pub upgrade
    $LOCAL_BIN/flutter build linux --release
    cd ..

    local OPTIMIZE_ARGS=()
    if [ "$PROFILING" = true ]; then
        OPTIMIZE_ARGS+=("--profiling")
    fi
    if [ -n "$HOST_PATH" ] && [ -d "$HOST_PATH" ]; then
        OPTIMIZE_ARGS+=("--host-path=$HOST_PATH")
    fi
    if [ -n "$LOCAL_BIN" ]; then
        OPTIMIZE_ARGS+=("--flutter-bin=$LOCAL_BIN")
    fi

    ./tools/optimize_bundle.sh "${OPTIMIZE_ARGS[@]}" \
        --project-dir=shell \
        shell/build/linux/x64/release/bundle \
        build/shell
}

function build_out() {
    mkdir -p out
    if [ "$SKIP_CLIENT" = false ] && [ -d build/shell ]; then
        cp -rfp build/shell out/
    fi
    if [ "$SKIP_SERVER" = false ] && [ -f build/src/sparrow ]; then
        cp -rfp build/src/sparrow out/
        if [ -f build/src/runner/sparrow-app-runner ]; then
            cp -rfp build/src/runner/sparrow-app-runner out/
            echo -e "Installed sparrow-app-runner into out/\n"
        fi
        mkdir -p out/shell/lib
        if [ -f build/subprojects/wlroots/libwlroots-0.20.so ]; then
            cp -rfp build/subprojects/wlroots/libwlroots-0.20.so out/shell/lib/
        fi
        if [ -n "$HOST_PATH" ] && [ -f "$HOST_PATH/libflutter_engine.so" ]; then
            cp -rfp "$HOST_PATH/libflutter_engine.so" out/shell/lib/
            echo -e "Installed custom engine (from $HOST_PATH) into out/shell/lib/\n"
        elif [ -f subprojects/flutter_embedder/libflutter_engine.so ]; then
            cp -rfp subprojects/flutter_embedder/libflutter_engine.so out/shell/lib/
            echo -e "Installed official embedder engine into out/shell/lib/\n"
        elif [ -f build/subprojects/flutter_embedder/libflutter_engine.so ]; then
            cp -rfp build/subprojects/flutter_embedder/libflutter_engine.so out/shell/lib/
            echo -e "Installed official embedder engine into out/shell/lib/\n"
        else
            echo -e "No custom engine specified via --host-path; dynamic linker will fallback to system library.\n"
        fi

        if [ -d build/subprojects/wleird ]; then
            mkdir -p out/wleird
            find build/subprojects/wleird -maxdepth 1 -type f -name "wleird-*" -exec cp -f {} out/wleird/ \;
            echo -e "Installed wleird stress-test suite into out/wleird/\n"
        fi

        if [ -f build-ck/compositor-killer ]; then
            cp -fp build-ck/compositor-killer out/
            echo -e "Installed compositor-killer into out/\n"
        fi
    fi
}

function generate_pigeon() {
    echo -e '\n=========================================='
    echo " Generating Pigeon APIs"
    echo -e "==========================================\n"

    if [ -z "$LOCAL_BIN" ]; then
        if which flutter >/dev/null 2>&1; then
            LOCAL_BIN="$(dirname "$(which flutter)")"
        elif [ -d "$HOME/.local/share/flutter/bin" ]; then
            LOCAL_BIN="$HOME/.local/share/flutter/bin"
        elif [ -d /opt/flutter/bin ]; then
            LOCAL_BIN="/opt/flutter/bin"
        else
            LOCAL_BIN=~/.local/share/flutter/bin
        fi
    fi

    cd platform/pigeon/compositor/pigeon_compositor
    $LOCAL_BIN/flutter clean
    $LOCAL_BIN/flutter pub get
    $LOCAL_BIN/flutter pub upgrade
    $LOCAL_BIN/dart run pigeon --input pigeons/messages.dart
    cd ../../runner/pigeon_runner
    $LOCAL_BIN/flutter clean
    $LOCAL_BIN/flutter pub get
    $LOCAL_BIN/flutter pub upgrade
    $LOCAL_BIN/dart run pigeon --input pigeons/messages.dart

    cd ../../../..
    echo -e '\n Pigeon generation complete!\n'
}

# --- Execution ---
if [ "$FORCE_REGENERATE_PIGEON" = false ]; then
    if [ -z "$(ls -A platform/pigeon/compositor/cpp)" ] ||
    [ -z "$(ls -A platform/pigeon/compositor/gobject)" ] ||
    [ -z "$(ls -A platform/pigeon/compositor/pigeon_compositor/lib/src)" ] ||
    [ -z "$(ls -A platform/pigeon/runner/cpp)" ] ||
    [ -z "$(ls -A platform/pigeon/runner/gobject)" ] ||
    [ -z "$(ls -A platform/pigeon/runner/pigeon_runner/lib/src)" ]; then
        FORCE_REGENERATE_PIGEON=true
        echo -e '\n Detected missing Pigeon APIs, regenerating...\n'
    fi
fi

if [ "$RUN_PIGEON" = true ] || [ "$FORCE_REGENERATE_PIGEON" = true ]; then
    echo -e '\n Re-generating Pigeon APIs...\n'
    generate_pigeon

    if [ "$RUN_PIGEON" = true ]; then
        exit 0
    fi

    if [ "$FORCE_REGENERATE_PIGEON" = false ] && [ "$SKIP_SERVER" = false ] && [ "$SKIP_CLIENT" = false ] && [ $# -eq 1 ]; then
        exit 0
    fi
fi

if [ "$SKIP_SERVER" = false ]; then
    build_server
fi

if [ "$SKIP_CLIENT" = false ]; then
    build_client
fi

build_out
echo -e "\n Sparrow build complete!\n"
