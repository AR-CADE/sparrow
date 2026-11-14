#!/usr/bin/env bash
# ==============================================================================
# Sparrow Automated PGO Pipeline
# Compiles Sparrow with profile instrumentation, drives an automated realistic
# test session using the UI bot, and compiles the final hyper-optimized release.
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${CYAN}${BOLD}======================================================${NC}"
echo -e "${CYAN}${BOLD}       Sparrow Profile-Guided Optimization (PGO)       ${NC}"
echo -e "${CYAN}${BOLD}======================================================${NC}"
echo ""

HEADLESS_FLAG="--headless"
for arg in "$@"; do
    case "$arg" in
        --no-headless|--gui)
            HEADLESS_FLAG=""
            ;;
        --headless|-H)
            HEADLESS_FLAG="--headless"
            ;;
    esac
done

cd "$ROOT_DIR"

# Step 1: Compile instrumented binary
if [ ! -f "${ROOT_DIR}/out/shell/app.so" ] || [ ! -d "${ROOT_DIR}/out/shell/data" ]; then
    echo -e "${YELLOW}${BOLD}[STEP 1/3] Compiling full release build with PGO instrumentation (server + client shell)...${NC}"
    ./build.sh pgo-generate
else
    echo -e "${YELLOW}${BOLD}[STEP 1/3] Compiling instrumented server (-fprofile-generate)...${NC}"
    ./build.sh pgo-generate server
fi
echo -e "${GREEN}[OK] Instrumented binary ready.${NC}\n"

# Step 2: Run training workloads
echo -e "${YELLOW}${BOLD}[STEP 2/3] Generating profile dataset via PageView/Overview & automated UI bot...${NC}"
PGO_PROFILE_DIR="${ROOT_DIR}/out/pgo_profiles"
rm -rf "$PGO_PROFILE_DIR"
mkdir -p "$PGO_PROFILE_DIR"

export LLVM_PROFILE_FILE="${PGO_PROFILE_DIR}/sparrow-%p.profraw"

echo -e "${CYAN}  -> [1/2] Running PageView & Overview multi-window workload...${NC}"
./tools/bot/pageview_overview_test.sh $HEADLESS_FLAG

echo -e "${CYAN}  -> [2/2] Running declarative application workload bot...${NC}"
./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG
echo -e "${GREEN}[OK] Profile datasets collected in ${PGO_PROFILE_DIR}.${NC}\n"

# Step 2.5: Merge raw profiles into sparrow.profdata
echo -e "${YELLOW}${BOLD}[STEP 2.5] Converting raw profiles to sparrow.profdata...${NC}"
if compgen -G "${PGO_PROFILE_DIR}/*.profraw" > /dev/null; then
    llvm-profdata merge -output="${ROOT_DIR}/sparrow.profdata" "${PGO_PROFILE_DIR}"/*.profraw
    echo -e "${GREEN}[OK] Generated sparrow.profdata ($(du -h "${ROOT_DIR}/sparrow.profdata" | cut -f1)).${NC}\n"
elif [ -f "${ROOT_DIR}/sparrow.profraw" ]; then
    llvm-profdata merge -output="${ROOT_DIR}/sparrow.profdata" "${ROOT_DIR}/sparrow.profraw"
    echo -e "${GREEN}[OK] Generated sparrow.profdata ($(du -h "${ROOT_DIR}/sparrow.profdata" | cut -f1)).${NC}\n"
else
    echo -e "${RED}[ERROR] No raw profile found in ${PGO_PROFILE_DIR} or ${ROOT_DIR}/sparrow.profraw!${NC}"
    exit 1
fi

# Step 3: Compile optimized release binary
echo -e "${YELLOW}${BOLD}[STEP 3/3] Compiling final optimized binary (-fprofile-use)...${NC}"
./build.sh optimize server
echo -e "${GREEN}[OK] PGO release build finished.${NC}\n"

echo -e "${GREEN}${BOLD}======================================================${NC}"
echo -e "${GREEN}${BOLD}  ✔ PGO Pipeline Completed Successfully!               ${NC}"
echo -e "${GREEN}${BOLD}======================================================${NC}"
ls -lh out/sparrow
