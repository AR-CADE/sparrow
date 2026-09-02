#!/bin/bash
set -e

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
    rm -rf out
    rm -rf sparrow.prof*
}

function show_help() {
    echo "Usage: ./build.sh [OPTIONS...]"
    echo ""
    echo "Build Modes (choose one base mode, default is release):"
    echo "  release (default)   Optimized release build (PGO + LTO enabled by default)"
    echo "  debug               Debug build with symbols, no optimizations"
    echo "  profile             Instrumented build to generate PGO profile"
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
    echo "  pgo / no-pgo        Enable / disable PGO in release mode"
    echo "  dmabuf              Enable direct DMA-buf (-Denable_dmabuf=true)"
    echo "  no-dmabuf           Disable direct DMA-buf (-Denable_dmabuf=false)"
    echo "  damage-history      Enable damage history tracking (-Denable_damage_history=true)"
    echo "  no-damage-history   Disable damage history tracking (-Denable_damage_history=false)"
    echo "  server / no-client  Build only the C++ server binary"
    echo "  client / no-server  Build only the Flutter client shell"
    echo "  --host-path=<path>  Path to custom engine artifacts (e.g. flutter/engine/host_profile)"
    echo "  clean               Remove build/ and out/ directories"
    echo ""
    echo "Examples:"
    echo "  ./build.sh                               # Release using system/downloaded engine"
    echo "  ./build.sh --host-path=flutter/engine/host_release # Release with custom engine"
    echo "  ./build.sh profile --host-path=flutter/engine/host_profile # Profile with VM Service"
    echo "  ./build.sh no-impeller                   # Release with Skia (Skia + DMA-BUF + PGO + LTO)"
    echo "  ./build.sh vulkan                        # Release (Vulkan + Impeller + PGO + LTO)"
    echo "  ./build.sh asan                          # ASan debug"
    echo "  ./build.sh profile                       # PGO profile generation"
    echo "  ./build.sh tsan                          # TSan debug"
    echo "  ./build.sh valgrind                      # Valgrind debug"
    echo "  ./build.sh debug server                  # Debug C++ server only"
    echo "  ./build.sh client                        # Rebuild Flutter client shell only"
}

# --- Default Configuration ---
BUILD_TYPE="release"
ENABLE_IMPELLER=true
ENABLE_VULKAN=false
ENABLE_DAMAGE_HISTORY=true
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
HOST_PATH=""
EXTRA_MESON_ARGS=()

# --- Parse Arguments ---
for arg in "$@"; do
    case "$arg" in
        clean)
            clean
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
        release|--release)
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
        server-only|--server-only|server|--server|no-client|--no-client)
            SKIP_CLIENT=true
            ;;
        client-only|--client-only|client|--client|no-server|--no-server)
            SKIP_SERVER=true
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

if [ "$PGO" = true ] && [ ! -f "sparrow.profdata" ]; then
    PGO=false
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

if [ ${#EXTRA_MESON_ARGS[@]} -gt 0 ]; then
    MESON_ARGS+=("${EXTRA_MESON_ARGS[@]}")
fi

function build_server() {
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

    LOCAL_BIN=~/.local/share/flutter/bin

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

    cp -rfp shell/build/linux/x64/release/bundle/data/ build/shell/ || echo "Error: Failed to copy shell data "
    cp -rfp shell/build/linux/x64/release/bundle/lib/* build/shell/lib/ || echo "Error: Failed to copy shell lib "
    rm -rf build/shell/lib/libapp.so
    rm -rf build/shell/lib/libflutter_linux_gtk.so

    LOCAL_ENGINE=$LOCAL_BIN/cache
    GEN_SNAPSHOT_BIN="$LOCAL_ENGINE/dart-sdk/bin/utils/gen_snapshot"
    PATCHED_SDK_PATH="$LOCAL_ENGINE/artifacts/engine/common/flutter_patched_sdk"

    DART_VM_FLAG="-Ddart.vm.product=true"
    GEN_SNAPSHOT_FLAGS="--obfuscate --strip"

    if [ "$PROFILING" = true ]; then
        if [ -n "$HOST_PATH" ] && [ -d "$HOST_PATH" ]; then
            DART_VM_FLAG="-Ddart.vm.profile=true"
            GEN_SNAPSHOT_FLAGS=""
            echo -e "Client mode: Profile with custom engine (Dart VM Service enabled with -Ddart.vm.profile=true)\n"
        else
            echo -e "Notice: Profile mode requested without custom engine artifacts (--host-path=...)."
            echo -e "Falling back to Release AOT product (-Ddart.vm.product=true) to match system engine.\n"
        fi
    else
        echo -e "Client mode: Release (AOT Product with -Ddart.vm.product=true)\n"
    fi

    if [ -n "$HOST_PATH" ] && [ -d "$HOST_PATH" ]; then
        if [ -f "$HOST_PATH/gen_snapshot" ]; then
            GEN_SNAPSHOT_BIN="$HOST_PATH/gen_snapshot"
        fi
        if [ -d "$HOST_PATH/flutter_patched_sdk" ]; then
            PATCHED_SDK_PATH="$HOST_PATH/flutter_patched_sdk"
        fi
        echo -e "Using custom engine artifacts from: $HOST_PATH\n"
    fi

    # Generate .dill snapshot
    echo -e '\nGenerate .dill snapshot...\n'
    $LOCAL_ENGINE/dart-sdk/bin/dartaotruntime \
    $LOCAL_ENGINE/dart-sdk/bin/snapshots/frontend_server_aot.dart.snapshot \
    --sdk-root "$PATCHED_SDK_PATH" \
    --target=flutter \
    --aot \
    --tfa \
    $DART_VM_FLAG \
    --packages shell/.dart_tool/package_config.json \
    --output-dill build/kernel_snapshot.dill \
    --verbose \
    --depfile build/kernel_snapshot.d \
    shell/lib/main.dart

    # Generate optimized app.so
    echo -e '\nGenerate app.so...\n'
    "$GEN_SNAPSHOT_BIN" \
    --deterministic \
    --snapshot_kind=app-aot-elf \
    $GEN_SNAPSHOT_FLAGS \
    --elf=build/shell/app.so \
    build/kernel_snapshot.dill
}

function build_out() {
    mkdir -p out
    if [ "$SKIP_CLIENT" = false ] && [ -d build/shell ]; then
        cp -rfp build/shell out/
    fi
    if [ "$SKIP_SERVER" = false ] && [ -f build/src/sparrow ]; then
        cp -rfp build/src/sparrow out/
        mkdir -p out/shell/lib
        if [ -f build/subprojects/wlroots/libwlroots-0.20.so ]; then
            cp -rfp build/subprojects/wlroots/libwlroots-0.20.so out/shell/lib/
        fi
        if [ -n "$HOST_PATH" ] && [ -f "$HOST_PATH/libflutter_engine.so" ]; then
            cp -rfp "$HOST_PATH/libflutter_engine.so" out/shell/lib/
            echo -e "Installed custom engine (from $HOST_PATH) into out/shell/lib/\n"
        else
            echo -e "No custom engine specified via --host-path; dynamic linker will fallback to system library.\n"
        fi
    fi
}

# --- Execution ---
if [ "$SKIP_SERVER" = false ]; then
    build_server
fi

if [ "$SKIP_CLIENT" = false ]; then
    build_client
fi

build_out
echo -e "\n Sparrow build complete!\n"
