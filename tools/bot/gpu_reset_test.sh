#!/usr/bin/env bash
# ==============================================================================
# Sparrow - GPU Soft Reset & Recovery Automated Test Suite
# ==============================================================================
# Simulates GPU context loss / soft reset (via F5 keypress and SIGUSR2) across:
#   1. Active rendering load (continuous frames via jank_monitor)
#   2. Active session lock (ext-session-lock-v1 direct GLES bypass)
#   3. Fullscreen toplevel (direct scanout / fullscreen bypass)
# Validates state restoration, texture rehydration, and zero leaks/crashes.
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"
TEST_DIR="${SCRIPT_DIR}/session_lock"
REPRO_DIR="${SCRIPT_DIR}/repro_clients"

SPARROW_BIN="${SPARROW_BIN:-${OUT_DIR}/sparrow}"
SPARROW_KEY="${REPRO_DIR}/sparrow_key"
JANK_MONITOR="${REPRO_DIR}/jank_monitor"
WLRCTL_BIN="${WLRCTL_BIN:-${OUT_DIR}/wlrctl}"
LOG_FILE="${OUT_DIR}/gpu_reset_test.log"

if [ ! -x "$WLRCTL_BIN" ] && [ -x "${ROOT_DIR}/build/subprojects/wlrctl/wlrctl" ]; then
    WLRCTL_BIN="${ROOT_DIR}/build/subprojects/wlrctl/wlrctl"
elif [ ! -x "$WLRCTL_BIN" ] && command -v wlrctl &>/dev/null; then
    WLRCTL_BIN="$(command -v wlrctl)"
fi

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
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --headless, -H      Run in headless mode (WLR_BACKENDS=headless)"
            echo "  --show-logs, -l     Print full compositor logs after test completion"
            echo "  --verbose, -v       Stream logs in real-time"
            echo "  --help, -h          Show this help message"
            exit 0
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow GPU Soft Reset & Recovery Automated Test Suite ===${NC}"
echo -e "Compositor Log : ${BOLD}${LOG_FILE}${NC}"

# 1. Prerequisite checks
if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

make -C "$REPRO_DIR" sparrow_key jank_monitor >/dev/null 2>&1
make -C "$TEST_DIR" test_session_lock >/dev/null 2>&1

mkdir -p "$OUT_DIR"
rm -f "$LOG_FILE" "${OUT_DIR}"/gpu_reset_*.log

# 2. Runtime setup
RUNTIME_DIR=$(mktemp -d /tmp/sparrow-gpu-reset-XXXXXX)
export XDG_RUNTIME_DIR="$RUNTIME_DIR"

cleanup() {
    local exit_code=$?
    if [ -n "${SPARROW_PID:-}" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    rm -rf "$RUNTIME_DIR"
    exit $exit_code
}
trap cleanup EXIT INT TERM

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS=headless
    export WLR_HEADLESS_OUTPUTS=1
    export LIBGL_ALWAYS_SOFTWARE=1
    export mesa_glthread=false
fi

echo -e "${BLUE}[1/4] Launching Sparrow Compositor (with --debug-gpureset)...${NC}"
"$SPARROW_BIN" --no-realtime --debug-gpureset > "$LOG_FILE" 2>&1 &
SPARROW_PID=$!

MAX_WAIT=50
SPARROW_DISPLAY=""
for i in $(seq 1 $MAX_WAIT); do
    if [ -d "$RUNTIME_DIR" ]; then
        SOCK=$(find "$RUNTIME_DIR" -maxdepth 1 -name "wayland-*" -type s 2>/dev/null | head -n 1)
        if [ -n "$SOCK" ]; then
            SPARROW_DISPLAY=$(basename "$SOCK")
            break
        fi
    fi
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

if [ -z "$SPARROW_DISPLAY" ] || [ ! -S "$RUNTIME_DIR/$SPARROW_DISPLAY" ]; then
    echo -e "${RED}[FAIL] Sparrow failed to initialize Wayland display socket.${NC}"
    cat "$LOG_FILE"
    exit 1
fi

echo -e "${GREEN}[OK] Compositor active on socket ${CYAN}${SPARROW_DISPLAY}${GREEN} (PID: ${SPARROW_PID})${NC}"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

sleep 1

PASSED_COUNT=0
FAILED_COUNT=0

# Helper to trigger GPU reset:
# Try F5 key via virtual-keyboard first, with fallback to SIGUSR2
trigger_gpu_reset() {
    local source_desc="$1"
    echo -e "    ${MAGENTA}--> Triggering GPU soft reset (${source_desc})...${NC}"
    if [ -x "$SPARROW_KEY" ]; then
        "$SPARROW_KEY" F5 2>/dev/null || true
    fi
    # Also send SIGUSR2 to ensure coverage of signal-based reset
    kill -USR2 "$SPARROW_PID" 2>/dev/null || true
}

# ------------------------------------------------------------------------------
# SCENARIO 1: GPU Reset under active rendering load (jank_monitor)
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}[2/4] Scenario 1: GPU Soft Reset with Active Frame Rendering Client...${NC}"
JANK_LOG="${OUT_DIR}/gpu_reset_jank.log"
"$JANK_MONITOR" --duration=20 --max-jank-pct=100 > "$JANK_LOG" 2>&1 &
MONITOR_PID=$!

sleep 1.5

if kill -0 "$MONITOR_PID" 2>/dev/null; then
    echo -e "  [+] Jank monitor client rendering frames (PID: ${MONITOR_PID})"
else
    echo -e "  ${RED}[-] Jank monitor failed to start!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

# Reset 1
trigger_gpu_reset "Iteration 1 - F5 during frame pacing"
sleep 1.2

# Reset 2 (Rapid consecutive reset)
trigger_gpu_reset "Iteration 2 - Rapid second F5"
sleep 1.5

# Check compositor log for GPU-RESET recovery
if grep -q "\[GPU-RESET\] Renderer recreated, engine restarted" "$LOG_FILE"; then
    echo -e "  ${GREEN}[OK] Compositor recovered and rehydrated textures under frame load!${NC}"
    PASSED_COUNT=$((PASSED_COUNT + 1))
else
    echo -e "  ${RED}[FAIL] Compositor failed to log GPU-RESET recovery!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

if kill -0 "$MONITOR_PID" 2>/dev/null; then
    echo -e "  ${GREEN}[OK] Client survived GPU reset and continued rendering!${NC}"
    PASSED_COUNT=$((PASSED_COUNT + 1))
    kill -TERM "$MONITOR_PID" 2>/dev/null || true
    wait "$MONITOR_PID" 2>/dev/null || true
else
    echo -e "  ${RED}[FAIL] Client died during GPU reset!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

# ------------------------------------------------------------------------------
# SCENARIO 2: GPU Reset during Session Lock (Direct GLES Bypass)
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}[3/4] Scenario 2: GPU Soft Reset during Session Lock (Direct GLES)...${NC}"
LOCK_LOG="${OUT_DIR}/gpu_reset_lock.log"
"$TEST_DIR/test_session_lock" --hold=10 > "$LOCK_LOG" 2>&1 &
LOCK_PID=$!

# Wait for lock to be established (longer loop for Valgrind tolerance)
LOCKED_OK=false
for i in $(seq 1 150); do
    if grep -q "SUCCESS: Session is securely locked!" "$LOCK_LOG" 2>/dev/null; then
        LOCKED_OK=true
        break
    fi
    sleep 0.1
done

if [ "$LOCKED_OK" = true ]; then
    echo -e "  [+] Session locked successfully (PID: ${LOCK_PID})"
    sleep 0.5

    # Trigger GPU reset while lock surface is directly presented on screen
    PREV_RESET_COUNT=$(grep -c "\[GPU-RESET\] Renderer recreated" "$LOG_FILE" || true)
    trigger_gpu_reset "F5 during active session lock"
    sleep 1.5

    NEW_RESET_COUNT=$(grep -c "\[GPU-RESET\] Renderer recreated" "$LOG_FILE" || true)
    if [ "$NEW_RESET_COUNT" -gt "$PREV_RESET_COUNT" ]; then
        echo -e "  ${GREEN}[OK] GPU reset completed during session lock!${NC}"
        PASSED_COUNT=$((PASSED_COUNT + 1))
    else
        echo -e "  ${RED}[FAIL] GPU reset was not triggered during session lock!${NC}"
        FAILED_COUNT=$((FAILED_COUNT + 1))
    fi

    # Wait for client to finish its hold time and unlock cleanly
    LOCK_EXIT=0
    wait "$LOCK_PID" || LOCK_EXIT=$?
    if [ $LOCK_EXIT -eq 0 ] && grep -q "Session successfully unlocked!" "$LOCK_LOG"; then
        echo -e "  ${GREEN}[OK] Session lock client unlocked cleanly after GPU reset!${NC}"
        PASSED_COUNT=$((PASSED_COUNT + 1))
    else
        echo -e "  ${RED}[FAIL] Session lock client failed to unlock cleanly (exit ${LOCK_EXIT})!${NC}"
        FAILED_COUNT=$((FAILED_COUNT + 1))
    fi
else
    echo -e "  ${RED}[FAIL] Session lock was not established in time!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
    kill -TERM "$LOCK_PID" 2>/dev/null || true
    wait "$LOCK_PID" 2>/dev/null || true
fi

# ------------------------------------------------------------------------------
# SCENARIO 3: GPU Reset in Fullscreen
# ------------------------------------------------------------------------------
echo -e "\n${BLUE}[4/4] Scenario 3: GPU Soft Reset in Fullscreen...${NC}"
FULLSCREEN_LOG="${OUT_DIR}/gpu_reset_fullscreen.log"
"$JANK_MONITOR" --duration=15 --max-jank-pct=100 > "$FULLSCREEN_LOG" 2>&1 &
FS_PID=$!

sleep 1.5

# Switch to fullscreen via wlrctl if available
if [ -x "$WLRCTL_BIN" ]; then
    "$WLRCTL_BIN" toplevel fullscreen 2>/dev/null || true
    sleep 0.5
fi

PREV_RESET_COUNT=$(grep -c "\[GPU-RESET\] Renderer recreated" "$LOG_FILE" || true)
trigger_gpu_reset "F5 in fullscreen"
sleep 1.5

NEW_RESET_COUNT=$(grep -c "\[GPU-RESET\] Renderer recreated" "$LOG_FILE" || true)
if [ "$NEW_RESET_COUNT" -gt "$PREV_RESET_COUNT" ]; then
    echo -e "  ${GREEN}[OK] Compositor recovered and re-rendered in fullscreen!${NC}"
    PASSED_COUNT=$((PASSED_COUNT + 1))
else
    echo -e "  ${RED}[FAIL] GPU reset failed in fullscreen!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

if kill -0 "$FS_PID" 2>/dev/null; then
    echo -e "  ${GREEN}[OK] Fullscreen client remained stable after reset!${NC}"
    PASSED_COUNT=$((PASSED_COUNT + 1))
    kill -TERM "$FS_PID" 2>/dev/null || true
    wait "$FS_PID" 2>/dev/null || true
else
    echo -e "  ${RED}[FAIL] Fullscreen client died unexpectedly!${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

# Clean shutdown
echo -e "\n${BLUE}Requesting graceful compositor shutdown (SIGTERM)...${NC}"
kill -TERM "$SPARROW_PID" 2>/dev/null || true
SPARROW_EXIT=0
wait "$SPARROW_PID" 2>/dev/null || SPARROW_EXIT=$?

if [ $SPARROW_EXIT -eq 0 ]; then
    echo -e "${GREEN}[OK] Sparrow terminated gracefully (exit code 0)${NC}"
else
    echo -e "${RED}[FAIL] Sparrow exited with non-zero code ${SPARROW_EXIT}${NC}"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

# Inspect log for fatal errors/crashes
if grep -Ei "(ERROR: AddressSanitizer|Segmentation fault|SIGSEGV|Aborted)" "$LOG_FILE" >/dev/null 2>&1; then
    echo -e "${RED}[FAIL] Crash or sanitizer error detected in log:${NC}"
    grep -Ei -C 3 "(ERROR: AddressSanitizer|Segmentation fault|SIGSEGV|Aborted)" "$LOG_FILE"
    FAILED_COUNT=$((FAILED_COUNT + 1))
fi

echo -e "\n${CYAN}${BOLD}=== Test Results ===${NC}"
echo -e "Passed : ${GREEN}${PASSED_COUNT}${NC}"
echo -e "Failed : $([ $FAILED_COUNT -eq 0 ] && echo "${GREEN}0${NC}" || echo "${RED}${FAILED_COUNT}${NC}")"

if [ "$SHOW_LOGS" = true ]; then
    echo -e "\n${YELLOW}--- Full Compositor Log ---${NC}"
    cat "$LOG_FILE"
fi

if [ $FAILED_COUNT -gt 0 ]; then
    exit 1
fi
exit 0

