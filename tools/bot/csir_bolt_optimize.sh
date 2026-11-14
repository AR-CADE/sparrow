#!/usr/bin/env bash
# ==============================================================================
# Sparrow Automated CSIR PGO + LTO + BOLT Pipeline
#
# Stage 1: LLVM IR PGO instrumentation (-fprofile-generate) & training
# Stage 2: Context-Sensitive IR PGO instrumentation (-fcs-profile-generate) & training
# Stage 3: High-performance release compile with CSIR profile & LTO (-Wl,--emit-relocs)
# Stage 4: Post-link binary layout optimization via LLVM BOLT (LBR branch sampling)
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
echo -e "${MAGENTA}${BOLD}    Sparrow CSIR PGO + LTO + BOLT Optimization Pipeline        ${NC}"
echo -e "${MAGENTA}${BOLD}================================================================${NC}"
echo ""

HEADLESS_FLAG="--headless"
SKIP_BOLT=false
BOLT_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --no-headless|--gui)
            HEADLESS_FLAG=""
            ;;
        --headless|-H)
            HEADLESS_FLAG="--headless"
            ;;
        --no-bolt|--skip-bolt)
            SKIP_BOLT=true
            ;;
        --bolt-only|bolt-only)
            BOLT_ONLY=true
            ;;
    esac
done

cd "$ROOT_DIR"

STAGE1_DIR="${ROOT_DIR}/out/csir_profiles/ir"
STAGE2_DIR="${ROOT_DIR}/out/csir_profiles/csir"
PERF_DATA="${ROOT_DIR}/out/perf.data"
BOLT_FDATA="${ROOT_DIR}/out/sparrow.fdata"

if [ "$BOLT_ONLY" = false ]; then
    # Ensure clean directory structure for profiles
    rm -rf "$STAGE1_DIR" "$STAGE2_DIR" "$PERF_DATA" "$BOLT_FDATA"
    mkdir -p "$STAGE1_DIR" "$STAGE2_DIR"
fi

if [ "$BOLT_ONLY" = false ]; then
# ------------------------------------------------------------------------------
# STEP 1/4: Compile Stage 1 (IR-PGO) & Run Training Workload
# ------------------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}[STEP 1/4] Compiling Stage 1: LLVM IR-PGO instrumented server...${NC}"
if [ ! -f "${ROOT_DIR}/out/shell/app.so" ] || [ ! -d "${ROOT_DIR}/out/shell/data" ]; then
    echo -e "${CYAN}  Building shell client first...${NC}"
    ./build.sh client
fi
./build.sh csir-stage1 server
echo -e "${GREEN}[OK] Stage 1 instrumented binary ready.${NC}\n"

echo -e "${YELLOW}${BOLD}[STEP 1.1] Training Stage 1 IR-PGO via PageView/Overview & Bot workload...${NC}"
echo -e "${CYAN}  -> [1/2] Running PageView & Overview multi-window workload...${NC}"
./tools/bot/pageview_overview_test.sh $HEADLESS_FLAG

echo -e "${CYAN}  -> [2/2] Running declarative application workload bot...${NC}"
./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG

echo -e "${YELLOW}${BOLD}[STEP 1.2] Merging Stage 1 profiles into sparrow_ir.profdata...${NC}"
if compgen -G "${STAGE1_DIR}/*.profraw" > /dev/null; then
    llvm-profdata merge -output="${ROOT_DIR}/sparrow_ir.profdata" "${STAGE1_DIR}"/*.profraw
    echo -e "${GREEN}[OK] Generated sparrow_ir.profdata ($(du -h "${ROOT_DIR}/sparrow_ir.profdata" | cut -f1)).${NC}\n"
else
    echo -e "${RED}[ERROR] No raw IR profiles found in ${STAGE1_DIR}!${NC}"
    exit 1
fi

# ------------------------------------------------------------------------------
# STEP 2/4: Compile Stage 2 (CSIR-PGO) & Run Identical Training Workload
# ------------------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}[STEP 2/4] Compiling Stage 2: Context-Sensitive IR instrumented server...${NC}"
./build.sh csir-stage2 server
echo -e "${GREEN}[OK] Stage 2 context-sensitive instrumented binary ready.${NC}\n"

echo -e "${YELLOW}${BOLD}[STEP 2.1] Training Stage 2 CSIR-PGO via IDENTICAL workload...${NC}"
echo -e "${CYAN}  -> [1/2] Running PageView & Overview multi-window workload...${NC}"
./tools/bot/pageview_overview_test.sh $HEADLESS_FLAG

echo -e "${CYAN}  -> [2/2] Running declarative application workload bot...${NC}"
./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG

echo -e "${YELLOW}${BOLD}[STEP 2.2] Merging Stage 1 + Stage 2 profiles into sparrow_csir.profdata...${NC}"
if compgen -G "${STAGE2_DIR}/*.profraw" > /dev/null; then
    llvm-profdata merge -output="${ROOT_DIR}/sparrow_csir.profdata" "${ROOT_DIR}/sparrow_ir.profdata" "${STAGE2_DIR}"/*.profraw
    echo -e "${GREEN}[OK] Generated sparrow_csir.profdata ($(du -h "${ROOT_DIR}/sparrow_csir.profdata" | cut -f1)).${NC}\n"
else
    echo -e "${RED}[ERROR] No raw CSIR profiles found in ${STAGE2_DIR}!${NC}"
    exit 1
fi

# ------------------------------------------------------------------------------
# STEP 3/4: Compile Final CSIR + LTO Release Binary (preserving relocations)
# ------------------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}[STEP 3/4] Compiling CSIR + LTO release binary (with BOLT relocations)...${NC}"
./build.sh csir-optimize server
echo -e "${GREEN}[OK] CSIR + LTO release binary ready in out/sparrow.${NC}\n"
else
    if [ ! -f "${ROOT_DIR}/out/sparrow" ]; then
        echo -e "${RED}[ERROR] out/sparrow not found. Cannot run BOLT only.${NC}"
        exit 1
    fi
    echo -e "${CYAN}[BOLT-ONLY] Skipping Stages 1-3, optimizing existing out/sparrow with BOLT.${NC}\n"
fi

# ------------------------------------------------------------------------------
# STEP 4/4: Post-Link Binary Optimization via LLVM BOLT
# ------------------------------------------------------------------------------
if [ "$SKIP_BOLT" = true ]; then
    echo -e "${YELLOW}[BOLT] Skipping post-link BOLT optimization (--skip-bolt specified).${NC}"
elif ! command -v llvm-bolt &> /dev/null || ! command -v perf2bolt &> /dev/null; then
    echo -e "${YELLOW}[BOLT WARNING] llvm-bolt or perf2bolt not found in PATH.${NC}"
    echo -e "${YELLOW}               Sparrow remains fully optimized with CSIR PGO + LTO.${NC}"
elif ! command -v perf &> /dev/null; then
    echo -e "${YELLOW}[BOLT WARNING] 'perf' tool not found in PATH. Skipping BOLT phase.${NC}"
else
    echo -e "${YELLOW}${BOLD}[STEP 4/4] Optimizing code layout via LLVM BOLT (LBR branch sampling)...${NC}"
    
    PERF_DATA1="${ROOT_DIR}/out/perf1.data"
    PERF_DATA2="${ROOT_DIR}/out/perf2.data"
    BOLT_FDATA1="${ROOT_DIR}/out/perf1.fdata"
    BOLT_FDATA2="${ROOT_DIR}/out/perf2.fdata"
    BOLT_FDATA="${ROOT_DIR}/out/sparrow.fdata"
    rm -f "$PERF_DATA1" "$PERF_DATA2" "$BOLT_FDATA1" "$BOLT_FDATA2" "$BOLT_FDATA"

    # [1/2] Profile PageView & Overview workload
    WRAPPER1="${ROOT_DIR}/out/sparrow_perf_wrapper1.sh"
    cat << EOF > "$WRAPPER1"
#!/usr/bin/env bash
trap 'kill -TERM "\$PERF_PID" 2>/dev/null' TERM INT
perf record -e cycles:u -j any,u -o "${PERF_DATA1}" -- "${ROOT_DIR}/out/sparrow" "\$@" &
PERF_PID=\$!
wait "\$PERF_PID" 2>/dev/null || true
exit 0
EOF
    chmod +x "$WRAPPER1"
    echo -e "${CYAN}  -> [1/2] Profiling PageView & Overview workload under perf LBR...${NC}"
    SPARROW_BIN="$WRAPPER1" ./tools/bot/pageview_overview_test.sh $HEADLESS_FLAG || true
    rm -f "$WRAPPER1"

    # [2/2] Profile declarative application bot workload
    WRAPPER2="${ROOT_DIR}/out/sparrow_perf_wrapper2.sh"
    cat << EOF > "$WRAPPER2"
#!/usr/bin/env bash
trap 'kill -TERM "\$PERF_PID" 2>/dev/null' TERM INT
perf record -e cycles:u -j any,u -o "${PERF_DATA2}" -- "${ROOT_DIR}/out/sparrow" "\$@" &
PERF_PID=\$!
wait "\$PERF_PID" 2>/dev/null || true
exit 0
EOF
    chmod +x "$WRAPPER2"
    echo -e "${CYAN}  -> [2/2] Profiling declarative application workload bot under perf LBR...${NC}"
    SPARROW_BIN="$WRAPPER2" ./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG || true
    rm -f "$WRAPPER2"

    FDATA_FILES=()
    if [ -f "$PERF_DATA1" ]; then
        echo -e "${CYAN}  -> Aggregating perf1 branch samples via perf2bolt...${NC}"
        perf2bolt -p "$PERF_DATA1" -o "$BOLT_FDATA1" -ignore-build-id "${ROOT_DIR}/out/sparrow" || true
        [ -f "$BOLT_FDATA1" ] && FDATA_FILES+=("$BOLT_FDATA1")
    fi
    if [ -f "$PERF_DATA2" ]; then
        echo -e "${CYAN}  -> Aggregating perf2 branch samples via perf2bolt...${NC}"
        perf2bolt -p "$PERF_DATA2" -o "$BOLT_FDATA2" -ignore-build-id "${ROOT_DIR}/out/sparrow" || true
        [ -f "$BOLT_FDATA2" ] && FDATA_FILES+=("$BOLT_FDATA2")
    fi

    if [ ${#FDATA_FILES[@]} -gt 1 ]; then
        echo -e "${CYAN}  -> Merging branch profiles with merge-fdata...${NC}"
        merge-fdata -o "$BOLT_FDATA" "${FDATA_FILES[@]}"
    elif [ ${#FDATA_FILES[@]} -eq 1 ]; then
        cp -f "${FDATA_FILES[0]}" "$BOLT_FDATA"
    fi

    if [ -f "$BOLT_FDATA" ]; then
        echo -e "${CYAN}  -> Executing llvm-bolt post-link optimizer...${NC}"
        llvm-bolt "${ROOT_DIR}/out/sparrow" \
            -o "${ROOT_DIR}/out/sparrow.bolt" \
            -data="$BOLT_FDATA" \
            -reorder-blocks=ext-tsp \
            -reorder-functions=cdsort \
            -split-functions \
            -split-all-cold \
            -dyno-stats

        if [ -f "${ROOT_DIR}/out/sparrow.bolt" ]; then
            echo -e "${CYAN}  -> Stripping final optimized binary...${NC}"
            if command -v llvm-strip &> /dev/null; then
                llvm-strip --strip-all "${ROOT_DIR}/out/sparrow.bolt"
            else
                strip --strip-all "${ROOT_DIR}/out/sparrow.bolt"
            fi
            mv -f "${ROOT_DIR}/out/sparrow.bolt" "${ROOT_DIR}/out/sparrow"
            echo -e "${GREEN}[OK] BOLT optimization applied successfully!${NC}\n"
        fi
        rm -f "$PERF_DATA1" "$PERF_DATA2" "$BOLT_FDATA1" "$BOLT_FDATA2" "$BOLT_FDATA"
    else
        echo -e "${YELLOW}[BOLT WARNING] perf branch profile was not produced. Keeping CSIR+LTO binary.${NC}"
    fi
fi

echo -e "${GREEN}${BOLD}================================================================${NC}"
echo -e "${GREEN}${BOLD}  ✔ CSIR PGO + LTO + BOLT Pipeline Completed Successfully!      ${NC}"
echo -e "${GREEN}${BOLD}================================================================${NC}"
ls -lh out/sparrow
