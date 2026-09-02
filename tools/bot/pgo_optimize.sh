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

HEADLESS_FLAG=""
for arg in "$@"; do
    case "$arg" in
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

# Step 2: Run training workload with Bot
echo -e "${YELLOW}${BOLD}[STEP 2/3] Generating profile dataset via automated UI bot...${NC}"
./tools/bot/sparrow_bot.sh --pgo $HEADLESS_FLAG
echo -e "${GREEN}[OK] Profile data collected in sparrow.profraw.${NC}\n"

# Step 2.5: Merge raw profile into sparrow.profdata
echo -e "${YELLOW}${BOLD}[STEP 2.5] Converting raw profile to sparrow.profdata...${NC}"
if [ -f "${ROOT_DIR}/sparrow.profraw" ]; then
    llvm-profdata merge -output="${ROOT_DIR}/sparrow.profdata" "${ROOT_DIR}/sparrow.profraw"
    echo -e "${GREEN}[OK] Generated sparrow.profdata ($(du -h "${ROOT_DIR}/sparrow.profdata" | cut -f1)).${NC}\n"
else
    echo -e "${RED}[ERROR] sparrow.profraw not found!${NC}"
    exit 1
fi

# Step 3: Compile optimized release binary
echo -e "${YELLOW}${BOLD}[STEP 3/3] Compiling final optimized binary (-fprofile-use)...${NC}"
./build.sh release server
echo -e "${GREEN}[OK] PGO release build finished.${NC}\n"

echo -e "${GREEN}${BOLD}======================================================${NC}"
echo -e "${GREEN}${BOLD}  ✔ PGO Pipeline Completed Successfully!               ${NC}"
echo -e "${GREEN}${BOLD}======================================================${NC}"
ls -lh out/sparrow
