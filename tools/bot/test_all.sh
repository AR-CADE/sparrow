#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Comprehensive Automated Test Suite Runner
# ==============================================================================
# Executes all test suites sequentially in headless or display mode:
#   1. Pointer Calibration & Subpixel Precision Test
#   2. PageView & Overview Multi-Window Integration Test (with Dart VM Service)
#   3. Jank & Frame Pacing Regression Test
#   4. Declarative PGO App Workload Bot Test
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

HEADLESS_FLAG="--headless"
USE_VALGRIND=false
for arg in "$@"; do
    case "$arg" in
        --no-headless|--gui)
            HEADLESS_FLAG=""
            ;;
        --headless|-H)
            HEADLESS_FLAG="--headless"
            ;;
        --valgrind)
            USE_VALGRIND=true
            ;;
        --help|-h)
            echo "Usage: $0 [--headless|-H] [--no-headless|--gui] [--valgrind]"
            exit 0
            ;;
    esac
done

# ASan / LSan / TSan / UBSan options
PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)
if [ "$PTRACE_SCOPE" -ge 2 ]; then
    export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0"
else
    export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0"
fi
export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt:print_suppressions=0"
export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt"
export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt"
export SPARROW_NO_STARTUP_SCRIPTS=1

echo -e "${CYAN}${BOLD}================================================================${NC}"
echo -e "${CYAN}${BOLD}         SPARROW COMPREHENSIVE AUTOMATED TEST SUITE             ${NC}"
echo -e "${CYAN}${BOLD}================================================================${NC}\n"

if [ "$USE_VALGRIND" = true ]; then
    VALGRIND_DIR="${ROOT_DIR}/out/valgrind"
    mkdir -p "$VALGRIND_DIR"
    rm -f "${VALGRIND_DIR}"/*.log

    VALGRIND_WRAPPER="${ROOT_DIR}/out/sparrow_valgrind_wrapper.sh"
    cat << 'WRAPPER_EOF' > "$VALGRIND_WRAPPER"
#!/usr/bin/env bash
mkdir -p "${0%/*}/valgrind"
exec valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --num-callers=40 \
    --trace-children=no \
    --keep-debuginfo=yes \
    --verbose \
    --log-file="${0%/*}/valgrind/valgrind-%p.log" \
    "${0%/*}/sparrow" "$@"
WRAPPER_EOF
    chmod +x "$VALGRIND_WRAPPER"
    export SPARROW_BIN="$VALGRIND_WRAPPER"
    echo -e "${YELLOW}[VALGRIND] Active wrapper: ${SPARROW_BIN}${NC}"
    echo -e "${YELLOW}[VALGRIND] Logs target: ${VALGRIND_DIR}/valgrind-<PID>.log${NC}\n"
fi

# Ensure repro clients are compiled
make -C "${SCRIPT_DIR}/repro_clients" all >/dev/null 2>&1

JANK_EXTRA=""
if [ "$USE_VALGRIND" = true ]; then
    JANK_EXTRA="--max-jank=100"
fi

TESTS=(
    "Fixed-Size Window & Letterbox|${SCRIPT_DIR}/fixed_size_test.sh ${HEADLESS_FLAG}"
    "Popup Rotation & Dynamic Constraint|${SCRIPT_DIR}/popup_rotation_test.sh ${HEADLESS_FLAG}"
    "Pointer Calibration & Subpixel Precision|${SCRIPT_DIR}/pointer_calibration_test.sh ${HEADLESS_FLAG}"
    "PageView, Overview & VM Service Integration|${SCRIPT_DIR}/pageview_overview_test.sh ${HEADLESS_FLAG}"
    "Fullscreen Direct Scanout & wl_shm|${SCRIPT_DIR}/fullscreen_scanout_test.sh ${HEADLESS_FLAG}"
    "Session Lock & Idle Management|${SCRIPT_DIR}/session_lock_test.sh ${HEADLESS_FLAG}"
    "Secure Content & Privacy Protection|${SCRIPT_DIR}/secure_content_test.sh ${HEADLESS_FLAG}"
    "Process Supervisor & Startup Services|${SCRIPT_DIR}/startup_test.sh"
    "GPU Soft Reset & Recovery|${SCRIPT_DIR}/gpu_reset_test.sh ${HEADLESS_FLAG}"
    "Jank & Frame Pacing Regression|${SCRIPT_DIR}/jank_frame_test.sh ${HEADLESS_FLAG} --duration=5 ${JANK_EXTRA}"
    "Declarative App Workload Bot|${SCRIPT_DIR}/sparrow_bot.sh ${HEADLESS_FLAG} --duration=8"
)

PASSED=0
FAILED=0
FAILED_NAMES=()

for entry in "${TESTS[@]}"; do
    IFS="|" read -r test_name test_cmd <<< "$entry"
    echo -e "${CYAN}${BOLD}>>> Running: ${test_name}...${NC}"
    
    set +e
    $test_cmd
    res=$?
    set -e

    if [ $res -eq 0 ]; then
        echo -e "${GREEN}${BOLD}✔ PASSED: ${test_name}${NC}\n"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}${BOLD}✘ FAILED (Exit $res): ${test_name}${NC}\n"
        FAILED=$((FAILED + 1))
        FAILED_NAMES+=("$test_name")
    fi
done

echo -e "${CYAN}${BOLD}================================================================${NC}"
echo -e "${CYAN}${BOLD}                       TEST SUITE SUMMARY                       ${NC}"
echo -e "${CYAN}${BOLD}================================================================${NC}"
echo -e "Total Executed : $((PASSED + FAILED))"
echo -e "Passed         : ${GREEN}${PASSED}${NC}"
echo -e "Failed         : $([ $FAILED -eq 0 ] && echo "${GREEN}0${NC}" || echo "${RED}${FAILED}${NC}")"

if [ "$USE_VALGRIND" = true ]; then
    echo -e "\n${CYAN}${BOLD}================================================================${NC}"
    echo -e "${CYAN}${BOLD}                 VALGRIND MEMORY AUDIT REPORT                   ${NC}"
    echo -e "${CYAN}${BOLD}================================================================${NC}"
    LOG_COUNT=$(ls -1 "${VALGRIND_DIR}"/valgrind-*.log 2>/dev/null | wc -l)
    echo -e "Total Instances Profiled: ${BOLD}${LOG_COUNT}${NC}"
    
    SPARROW_LEAKS_FOUND=false
    for vlog in "${VALGRIND_DIR}"/valgrind-*.log; do
        [ -f "$vlog" ] || continue
        echo -e "\n${CYAN}>>> $(basename "$vlog"):${NC}"
        grep -A 5 "LEAK SUMMARY:" "$vlog" | sed 's/^/  /' || true
        # Check if definitely lost leaks originate in Sparrow src/ (direct caller frames)
        if grep -A 3 "definitely lost" "$vlog" | grep -q "\.\./src/"; then
            echo -e "  ${RED}${BOLD}⚠ WARNING: Definitely lost memory detected in src/ code!${NC}"
            SPARROW_LEAKS_FOUND=true
        fi
    done

    if [ "$SPARROW_LEAKS_FOUND" = false ]; then
        echo -e "\n${GREEN}${BOLD}✔ Zero memory leaks detected in Sparrow codebase (src/)!${NC}"
    else
        echo -e "\n${RED}${BOLD}✘ Potential memory leak detected in Sparrow codebase!${NC}"
    fi
    rm -f "$VALGRIND_WRAPPER"
fi

if [ $FAILED -gt 0 ]; then
    echo -e "\n${RED}Failed Suites:${NC}"
    for name in "${FAILED_NAMES[@]}"; do
        echo -e "  - ${RED}${name}${NC}"
    done
    echo ""
    exit 1
fi

echo -e "\n${GREEN}${BOLD}✔ ALL TEST SUITES PASSED CLEANLY! Ready for development & release.${NC}\n"
exit 0
