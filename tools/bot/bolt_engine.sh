#!/usr/bin/env bash
# ==============================================================================
# Sparrow LLVM BOLT Optimization for libflutter_engine.so
#
# Profiles and optimizes the Flutter Engine shared library using hardware LBR
# branch sampling from the Sparrow integration test suites.
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${MAGENTA}${BOLD}================================================================${NC}"
echo -e "${MAGENTA}${BOLD}   Flutter Engine (libflutter_engine.so) LLVM BOLT Optimizer   ${NC}"
echo -e "${MAGENTA}${BOLD}================================================================${NC}"
echo ""

UNSTRIPPED_ENGINE="${ROOT_DIR}/flutter/engine/host_release/lib.unstripped/libflutter_engine.so"
TARGET_ENGINE="${ROOT_DIR}/flutter/engine/host_release/libflutter_engine.so"
BACKUP_ENGINE="${ROOT_DIR}/flutter/engine/host_release/libflutter_engine.so.orig"
OUT_ENGINE="${ROOT_DIR}/out/shell/lib/libflutter_engine.so"

REVERT=false
HEADLESS_FLAG="--headless"
NO_RETRAIN=false
for arg in "$@"; do
    case "$arg" in
        --revert|revert)
            REVERT=true
            ;;
        --no-headless|--gui)
            HEADLESS_FLAG=""
            ;;
        --headless|-H)
            HEADLESS_FLAG="--headless"
            ;;
        --quick|--no-retrain|no-retrain)
            NO_RETRAIN=true
            ;;
    esac
done

cd "$ROOT_DIR"

if [ "$REVERT" = true ]; then
    if [ -f "$BACKUP_ENGINE" ]; then
        echo -e "${YELLOW}Reverting to original libflutter_engine.so...${NC}"
        cp -fp "$BACKUP_ENGINE" "$TARGET_ENGINE"
        [ -d "${ROOT_DIR}/out/shell/lib" ] && cp -fp "$BACKUP_ENGINE" "$OUT_ENGINE"
        echo -e "${GREEN}[OK] Reverted successfully to original engine.${NC}"
        exit 0
    else
        echo -e "${RED}[ERROR] No backup found at ${BACKUP_ENGINE}.${NC}"
        exit 1
    fi
fi

# Verify unstripped engine existence
if [ ! -f "$UNSTRIPPED_ENGINE" ]; then
    echo -e "${RED}[ERROR] Unstripped engine not found at:${NC}"
    echo -e "        ${UNSTRIPPED_ENGINE}"
    echo -e "        Cannot apply BOLT without unstripped symbol table."
    exit 1
fi

# Verify tools
for tool in llvm-bolt perf2bolt merge-fdata perf; do
    if ! command -v "$tool" &> /dev/null; then
        echo -e "${RED}[ERROR] Required tool '$tool' not found in PATH.${NC}"
        exit 1
    fi
done

# Ensure sparrow binary exists in out/
if [ ! -f "${ROOT_DIR}/out/sparrow" ]; then
    echo -e "${YELLOW}[NOTICE] out/sparrow not found. Building release server...${NC}"
    ./build.sh server
fi

# Ensure directories
mkdir -p "${ROOT_DIR}/out/bolt_engine"
PERF_DATA1="${ROOT_DIR}/out/bolt_engine/engine_perf1.data"
PERF_DATA2="${ROOT_DIR}/out/bolt_engine/engine_perf2.data"
FDATA1="${ROOT_DIR}/out/bolt_engine/engine1.fdata"
FDATA2="${ROOT_DIR}/out/bolt_engine/engine2.fdata"
FDATA_MERGED="${ROOT_DIR}/out/bolt_engine/engine_merged.fdata"
BOLT_OUTPUT="${ROOT_DIR}/out/bolt_engine/libflutter_engine.so.bolt"

if [ "$NO_RETRAIN" = true ] && [ -f "$FDATA_MERGED" ]; then
    echo -e "${CYAN}[QUICK] Using existing merged branch profile: ${FDATA_MERGED}${NC}\n"
else
    rm -f "$PERF_DATA1" "$PERF_DATA2" "$FDATA1" "$FDATA2" "$FDATA_MERGED" "$BOLT_OUTPUT"

    echo -e "${YELLOW}${BOLD}[1/4] Capturing Flutter Engine LBR branch execution profile...${NC}"

    # Profile Workload 1: PageView & Overview
    WRAPPER1="${ROOT_DIR}/out/bolt_engine/sparrow_perf_wrapper1.sh"
    cat << EOF > "$WRAPPER1"
#!/usr/bin/env bash
trap 'kill -TERM "\$PERF_PID" 2>/dev/null' TERM INT
perf record -e cycles:u -j any,u -o "${PERF_DATA1}" -- "${ROOT_DIR}/out/sparrow" "\$@" &
PERF_PID=\$!
wait "\$PERF_PID" 2>/dev/null || true
exit 0
EOF
    chmod +x "$WRAPPER1"

    echo -e "${CYAN}  -> [1/2] Profiling PageView & Overview workload...${NC}"
    SPARROW_BIN="$WRAPPER1" ./tools/bot/pageview_overview_test.sh $HEADLESS_FLAG || true
    rm -f "$WRAPPER1"

    # Profile Workload 2: Application bot
    WRAPPER2="${ROOT_DIR}/out/bolt_engine/sparrow_perf_wrapper2.sh"
    cat << EOF > "$WRAPPER2"
#!/usr/bin/env bash
trap 'kill -TERM "\$PERF_PID" 2>/dev/null' TERM INT
perf record -e cycles:u -j any,u -o "${PERF_DATA2}" -- "${ROOT_DIR}/out/sparrow" "\$@" &
PERF_PID=\$!
wait "\$PERF_PID" 2>/dev/null || true
exit 0
EOF
    chmod +x "$WRAPPER2"

    echo -e "${CYAN}  -> [2/2] Profiling declarative application bot workload...${NC}"
    SPARROW_BIN="$WRAPPER2" ./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG || true
    rm -f "$WRAPPER2"

    # ------------------------------------------------------------------------------
    # STEP 2/4: Aggregating perf branch samples with perf2bolt
    # ------------------------------------------------------------------------------
    echo -e "\n${YELLOW}${BOLD}[2/4] Aggregating LBR samples into BOLT branch profile...${NC}"

    FDATA_FILES=()
    if [ -f "$PERF_DATA1" ]; then
        echo -e "${CYAN}  -> Processing perf1 samples for libflutter_engine...${NC}"
        perf2bolt -p "$PERF_DATA1" -o "$FDATA1" "$UNSTRIPPED_ENGINE" || true
        [ -f "$FDATA1" ] && FDATA_FILES+=("$FDATA1")
    fi

    if [ -f "$PERF_DATA2" ]; then
        echo -e "${CYAN}  -> Processing perf2 samples for libflutter_engine...${NC}"
        perf2bolt -p "$PERF_DATA2" -o "$FDATA2" "$UNSTRIPPED_ENGINE" || true
        [ -f "$FDATA2" ] && FDATA_FILES+=("$FDATA2")
    fi

    if [ ${#FDATA_FILES[@]} -gt 1 ]; then
        echo -e "${CYAN}  -> Merging branch profiles via merge-fdata...${NC}"
        merge-fdata -o "$FDATA_MERGED" "${FDATA_FILES[@]}"
    elif [ ${#FDATA_FILES[@]} -eq 1 ]; then
        cp -f "${FDATA_FILES[0]}" "$FDATA_MERGED"
    else
        echo -e "${RED}[ERROR] No profile data generated for libflutter_engine.so.${NC}"
        exit 1
    fi
fi

# ------------------------------------------------------------------------------
# STEP 3/4: Optimizing libflutter_engine.so with llvm-bolt
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}${BOLD}[3/4] Optimizing libflutter_engine.so layout via llvm-bolt...${NC}"

# Note: -reorder-functions is omitted because shared library does not have --emit-relocs.
# Basic block layout reordering (-reorder-blocks=ext-tsp) and cold block splitting
# (-split-functions -split-all-cold) deliver the primary cache/branch performance gains.
llvm-bolt "$UNSTRIPPED_ENGINE" \
    -o "$BOLT_OUTPUT" \
    -data="$FDATA_MERGED" \
    -reorder-blocks=ext-tsp \
    -split-functions \
    -split-all-cold \
    -dyno-stats

if [ ! -f "$BOLT_OUTPUT" ]; then
    echo -e "${RED}[ERROR] llvm-bolt failed to produce optimized engine.${NC}"
    exit 1
fi

# ------------------------------------------------------------------------------
# STEP 4/4: Stripping and Installing BOLT-optimized Engine
# ------------------------------------------------------------------------------
echo -e "\n${YELLOW}${BOLD}[4/4] Stripping and installing optimized Flutter Engine...${NC}"

if command -v llvm-strip &> /dev/null; then
    llvm-strip --strip-all "$BOLT_OUTPUT"
else
    strip --strip-all "$BOLT_OUTPUT"
fi

# Backup original engine
if [ ! -f "$BACKUP_ENGINE" ] && [ -f "$TARGET_ENGINE" ]; then
    echo -e "${CYAN}  -> Backing up original engine to ${BACKUP_ENGINE}...${NC}"
    cp -fp "$TARGET_ENGINE" "$BACKUP_ENGINE"
fi

# Deploy
cp -fp "$BOLT_OUTPUT" "$TARGET_ENGINE"
mkdir -p "${ROOT_DIR}/out/shell/lib"
cp -fp "$BOLT_OUTPUT" "$OUT_ENGINE"

echo -e "\n${GREEN}${BOLD}================================================================${NC}"
echo -e "${GREEN}${BOLD}  ✔ libflutter_engine.so BOLT Optimization Complete!             ${NC}"
echo -e "${GREEN}${BOLD}================================================================${NC}"
ls -lh "$TARGET_ENGINE"
echo -e "${CYAN}Original engine backed up at: ${BACKUP_ENGINE}${NC}"
echo -e "${CYAN}To revert at any time, run: ./build.sh bolt-engine --revert${NC}\n"
