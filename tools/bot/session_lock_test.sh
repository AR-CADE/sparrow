#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Session Lock & Idle Management Automated Test Suite
# ==============================================================================
# Validates ext-session-lock-v1, ext-idle-notify-v1, zwp_idle_inhibit_v1,
# keyboard focus isolation under lock, and redraw-based idle prevention.
# Detects crashes, ASan memory errors, UBSan undefined behaviors, and TSan races.
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"
TEST_DIR="${SCRIPT_DIR}/session_lock"
SPARROW_BIN="${OUT_DIR}/sparrow"
LOG_FILE="${OUT_DIR}/session_lock_test.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

HEADLESS=false
SHOW_LOGS=false
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        --headless|-H)
            HEADLESS=true
            ;;
        --show-logs|-l)
            SHOW_LOGS=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --build|-b)
            echo -e "${BLUE}[BUILD] Building session lock test binaries...${NC}"
            make -C "$TEST_DIR"
            exit $?
            ;;
        --clean|-c)
            echo -e "${BLUE}[CLEAN] Cleaning session lock test binaries...${NC}"
            make -C "$TEST_DIR" clean
            exit $?
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --headless, -H      Run in headless mode (WLR_BACKENDS=headless, for CI/docker)"
            echo "  --show-logs, -l     Print full compositor logs after test completion"
            echo "  --verbose, -v       Stream logs in real-time"
            echo "  --build, -b         Build test binaries only (make)"
            echo "  --clean, -c         Clean test binaries (make clean)"
            echo "  --help, -h          Show this help message"
            exit 0
            ;;
    esac
done

detect_sanitizer() {
    local bin="$1"
    local syms
    syms=$(nm "$bin" 2>/dev/null || true)
    if [[ "$syms" == *"__asan_init"* ]]; then
        echo "AddressSanitizer (ASan)"
    elif [[ "$syms" == *"__tsan_init"* ]]; then
        echo "ThreadSanitizer (TSan)"
    elif [[ "$syms" == *"__ubsan_handle"* ]]; then
        echo "UndefinedBehaviorSanitizer (UBSan)"
    else
        echo "Standard (No Sanitizer)"
    fi
}

SPARROW_SANITIZER=$(detect_sanitizer "$SPARROW_BIN")

echo -e "${CYAN}${BOLD}======================================================${NC}"
echo -e "${CYAN}${BOLD}   Sparrow Session Lock & Idle Test Suite (Bot)       ${NC}"
echo -e "${CYAN}${BOLD}======================================================${NC}"
echo -e "Target Binary : ${BOLD}${SPARROW_BIN}${NC}"
echo -e "Sanitizer     : ${BOLD}${SPARROW_SANITIZER}${NC}"
echo -e "Mode          : $([ "$HEADLESS" = true ] && echo "${YELLOW}Headless${NC}" || echo "${GREEN}Standard${NC}")"

# 1. Prerequisite checks
if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    echo "Run ./build.sh first."
    exit 1
fi

echo -e "${BLUE}[1/4] Building session lock test suite binaries...${NC}"
make -C "$TEST_DIR" > /dev/null

mkdir -p "$OUT_DIR"
rm -f "$LOG_FILE"

# 2. Environment Setup
CLEANUP_RUNTIME_DIR=false
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    RUNTIME_DIR=$(mktemp -d /tmp/sparrow-lock-test-XXXXXX)
    chmod 0700 "$RUNTIME_DIR"
    export XDG_RUNTIME_DIR="$RUNTIME_DIR"
    CLEANUP_RUNTIME_DIR=true
else
    RUNTIME_DIR="$XDG_RUNTIME_DIR"
fi

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS="headless"
    export WLR_HEADLESS_OUTPUTS="1"
    unset WAYLAND_DISPLAY
    unset DISPLAY
fi

# Ensure WAYLAND_DISPLAY actually points to a live socket if set
if [ -n "${WAYLAND_DISPLAY:-}" ] && [ ! -S "$RUNTIME_DIR/$WAYLAND_DISPLAY" ]; then
    unset WAYLAND_DISPLAY
fi

# Hardware DRM probe
for node in /dev/dri/renderD128 /dev/dri/renderD129 /dev/dri/renderD130 /dev/dri/renderD131; do
    if [ -e "$node" ] && [ -r "$node" ] && [ -w "$node" ]; then
        export WLR_RENDER_DRM_DEVICE="$node"
        break
    fi
done
export mesa_glthread="false"

# ASan / LSan / TSan / UBSan suppression options
PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)
if [ "$PTRACE_SCOPE" -ge 2 ]; then
    export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0"
else
    export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0"
fi
export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt:print_suppressions=0"
export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt"
export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt"

cleanup() {
    if [ -n "${SPARROW_PID:-}" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [ "$CLEANUP_RUNTIME_DIR" = true ] && [ -d "$RUNTIME_DIR" ]; then
        rm -rf "$RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

echo -e "${BLUE}[2/4] Launching Sparrow compositor...${NC}"
cd "$ROOT_DIR"
"$SPARROW_BIN" --no-realtime > "$LOG_FILE" 2>&1 &
SPARROW_PID=$!

SPARROW_DISPLAY=""
for _ in $(seq 1 60); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$LOG_FILE" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$LOG_FILE" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$RUNTIME_DIR/$SPARROW_DISPLAY" ]; then
            break
        fi
    fi
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [ -z "$SPARROW_DISPLAY" ] || [ ! -S "$RUNTIME_DIR/$SPARROW_DISPLAY" ]; then
    echo -e "${RED}[FAIL] Sparrow failed to initialize Wayland display socket within 6s.${NC}"
    echo "Log output:"
    cat "$LOG_FILE"
    exit 1
fi

echo -e "${GREEN}[OK] Compositor active on socket ${CYAN}${SPARROW_DISPLAY}${GREEN} (PID: ${SPARROW_PID})${NC}"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

echo -e "${BLUE}[3/4] Executing test suite...${NC}"

PASSED_COUNT=0
FAILED_COUNT=0

run_test() {
    local test_name="$1"
    local test_bin="${TEST_DIR}/$test_name"
    local description="$2"

    echo -ne "  -> Running ${BOLD}${test_name}${NC} (${description})... "

    local test_out
    test_out=$("$test_bin" 2>&1) && local status=0 || local status=$?

    if [ $status -eq 0 ]; then
        echo -e "${GREEN}PASSED${NC}"
        PASSED_COUNT=$((PASSED_COUNT + 1))
        if [ "$VERBOSE" = true ]; then
            echo "$test_out" | sed 's/^/     /'
        fi
    else
        echo -e "${RED}FAILED (exit code ${status})${NC}"
        FAILED_COUNT=$((FAILED_COUNT + 1))
        echo -e "${RED}Test Output:${NC}"
        echo "$test_out" | sed 's/^/     /'
    fi
}

run_test "test_session_lock" "Protocol negotiation, lock surface lifecycle & unlock"
run_test "test_session_lock_focus" "Exclusive keyboard focus immunity & key routing"
run_test "test_redraw_idle" "Active window redraws keep compositor awake"
run_test "test_cursor_idle" "Blinking text cursor noise does not inhibit sleep"

# 4. Graceful Shutdown & Sanitizer Inspection
echo -e "${BLUE}[4/4] Requesting graceful compositor shutdown (SIGTERM)...${NC}"
kill -TERM "$SPARROW_PID"

SPARROW_EXIT_CODE=0
wait "$SPARROW_PID" || SPARROW_EXIT_CODE=$?

check_errors() {
    local log="$1"
    local has_error=false

    # ASan
    if grep -Ei "(ERROR: AddressSanitizer|heap-use-after-free|heap-buffer-overflow|global-buffer-overflow|stack-use-after-return)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # LSan
    if grep -Ei "(ERROR: LeakSanitizer: detected memory leaks)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # TSan
    if grep -Ei "(WARNING: ThreadSanitizer: data race)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # UBSan
    if grep -Ei "(runtime error:)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # Hard crashes
    if grep -Ei "(Segmentation fault|SIGSEGV|Fatal crash|corrupted size|double free)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi

    if [ "$has_error" = true ]; then
        echo "ERROR"
    else
        echo "CLEAN"
    fi
}

SPARROW_STATUS=$(check_errors "$LOG_FILE")
SHUTDOWN_CLEAN=false
if grep -F "Shutdown successful!" "$LOG_FILE" > /dev/null 2>&1; then
    SHUTDOWN_CLEAN=true
fi

echo ""
echo -e "${BOLD}======================================================${NC}"
echo -e "${BOLD}                  TEST RESULTS SUMMARY                ${NC}"
echo -e "${BOLD}======================================================${NC}"
echo -e "Tests Passed      : ${GREEN}${PASSED_COUNT}/$((PASSED_COUNT + FAILED_COUNT))${NC}"
echo -e "Sparrow Exit Code : $([ $SPARROW_EXIT_CODE -eq 0 ] && echo -e "${GREEN}0 (SUCCESS)${NC}" || echo -e "${RED}${SPARROW_EXIT_CODE} (FAIL)${NC}")"
echo -e "Shutdown State    : $([ "$SHUTDOWN_CLEAN" = true ] && echo -e "${GREEN}Clean (Shutdown successful!)${NC}" || echo -e "${RED}Incomplete${NC}")"
echo -e "Sanitizer Status  : $([ "$SPARROW_STATUS" = "CLEAN" ] && echo -e "${GREEN}CLEAN (0 sanitizer errors / leaks)${NC}" || echo -e "${RED}ERROR / LEAK DETECTED${NC}")"
echo -e "${BOLD}======================================================${NC}"

if [ "$SHOW_LOGS" = true ]; then
    echo -e "\n${BOLD}--- Compositor Log Output ---${NC}"
    cat "$LOG_FILE"
fi

if [ $FAILED_COUNT -eq 0 ] && [ $SPARROW_EXIT_CODE -eq 0 ] && \
   [ "$SHUTDOWN_CLEAN" = true ] && [ "$SPARROW_STATUS" = "CLEAN" ]; then
    echo -e "\n${GREEN}${BOLD}✔ ALL SESSION LOCK & IDLE TESTS PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ TEST FAILED! Examine logs in ${LOG_FILE}.${NC}\n"
    tail -n 30 "$LOG_FILE"
    exit 1
fi
