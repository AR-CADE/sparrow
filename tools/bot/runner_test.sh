#!/usr/bin/env bash
# ==============================================================================
# Sparrow - App Runner & Compositor Automated Sanitizer Test Suite
# ==============================================================================
# Launches Sparrow compositor and sparrow-app-runner concurrently.
# Verifies Wayland integration, Zero-Trust kernel socketpair IPC, synthetic
# user inputs, and monitors both processes for ASan, UBSan, and TSan errors.
# Generates two separate logs:
#   1. out/sparrow_test.log (Compositor)
#   2. out/runner_test.log  (Flutter App Runner)
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
RUNNER_BIN="${OUT_DIR}/sparrow-app-runner"
DEFAULT_APP="${OUT_DIR}/demo_app"

SPARROW_LOG="${OUT_DIR}/sparrow_test.log"
RUNNER_LOG="${OUT_DIR}/runner_test.log"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

DURATION_SEC=8
APP_BUNDLE="$DEFAULT_APP"
HEADLESS=false
VERBOSE=false
SHOW_LOGS=false
USE_VULKAN=false
VK_VALIDATION=false
VK_PRESENT_MODE=""

for arg in "$@"; do
    case "$arg" in
        --duration=*|-d=*)
            DURATION_SEC="${arg#*=}"
            ;;
        --app=*|-a=*)
            APP_BUNDLE="${arg#*=}"
            ;;
        --headless|-H)
            HEADLESS=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --show-logs|-l)
            SHOW_LOGS=true
            ;;
        --vulkan)
            USE_VULKAN=true
            ;;
        --vk-validation)
            VK_VALIDATION=true
            USE_VULKAN=true
            ;;
        --vk-present-mode=*)
            VK_PRESENT_MODE="${arg#*=}"
            USE_VULKAN=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --duration=N, -d=N   Test execution duration in seconds (default: 8s)"
            echo "  --app=PATH, -a=PATH  Path to Flutter bundle to test (default: out/demo_app)"
            echo "  --headless, -H       Run compositor in headless mode (WLR_BACKENDS=headless)"
            echo "  --vulkan             Run sparrow-app-runner with native Vulkan renderer"
            echo "  --vk-validation      Run runner with Khronos validation layers enabled"
            echo "  --vk-present-mode=M  Swapchain present mode: fifo (default), mailbox, immediate"
            echo "  --verbose, -v        Stream runner and compositor events in real time"
            echo "  --show-logs, -l      Display full logs at the end of execution"
            echo "  --help, -h           Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow Runner & Compositor Automated Sanitizer Test Suite ===${NC}"
echo -e "Target Compositor : ${BOLD}${SPARROW_BIN}${NC}"
echo -e "Target Runner     : ${BOLD}${RUNNER_BIN}${NC}"
echo -e "Target App Bundle : ${BOLD}${APP_BUNDLE}${NC}"
echo -e "Compositor Log    : ${BOLD}${SPARROW_LOG}${NC}"
echo -e "Runner Log        : ${BOLD}${RUNNER_LOG}${NC}"
echo -e "Test Duration     : ${BOLD}${DURATION_SEC}s${NC}"

# 1. Prerequisite verification
if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    echo "Run ./build.sh first."
    exit 1
fi

if [ ! -f "$RUNNER_BIN" ]; then
    echo -e "${RED}[ERROR] Runner binary not found at ${RUNNER_BIN}.${NC}"
    echo "Run ./build.sh first."
    exit 1
fi

if [ ! -d "$APP_BUNDLE" ]; then
    echo -e "${RED}[ERROR] App bundle not found at ${APP_BUNDLE}.${NC}"
    echo "Run ./tools/optimize_bundle.sh to create the test bundle."
    exit 1
fi

# 2. Detect Sanitizer Modes for Both Binaries
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
RUNNER_SANITIZER=$(detect_sanitizer "$RUNNER_BIN")

echo -e "Sparrow Sanitizer : ${BOLD}${SPARROW_SANITIZER}${NC}"
echo -e "Runner Sanitizer  : ${BOLD}${RUNNER_SANITIZER}${NC}"

# Setup sanitizer suppressions
if [ -f "${ROOT_DIR}/lsan_suppressions.txt" ]; then
    if [ -z "$LSAN_OPTIONS" ]; then
        export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt"
    elif [[ "$LSAN_OPTIONS" != *"suppressions="* ]]; then
        export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt:${LSAN_OPTIONS}"
    fi
fi

if [ -f "${ROOT_DIR}/tsan_suppressions.txt" ]; then
    if [ -z "$TSAN_OPTIONS" ]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:abort_on_error=1:report_signal_unsafe=0:second_deadlock_stack=1"
    elif [[ "$TSAN_OPTIONS" != *"suppressions="* ]]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:report_signal_unsafe=0:second_deadlock_stack=1:${TSAN_OPTIONS}"
    fi
fi

if [ -f "${ROOT_DIR}/ubsan_suppressions.txt" ]; then
    if [ -z "$UBSAN_OPTIONS" ]; then
        export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt:print_stacktrace=1"
    elif [[ "$UBSAN_OPTIONS" != *"suppressions="* ]]; then
        export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt:${UBSAN_OPTIONS}"
    fi
fi

PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)

if [[ "$SPARROW_SANITIZER" == *"ASan"* ]] || [[ "$RUNNER_SANITIZER" == *"ASan"* ]]; then
    if [ -z "$ASAN_OPTIONS" ]; then
        export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0"
    elif [[ "$ASAN_OPTIONS" != *"detect_leaks="* ]]; then
        export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_fatal=1:detect_odr_violation=0:${ASAN_OPTIONS}"
    fi
fi

# Compositor-specific ASAN options: when Yama ptrace_scope >= 2, non-root processes
# are strictly blocked from ptracing non-descendants. Background Mesa GPU driver threads
# cause LSan to abort with EPERM. In this case, detect_leaks=0 for the compositor process.
SPARROW_ASAN_OPTIONS="$ASAN_OPTIONS"
if [ "$PTRACE_SCOPE" -ge 2 ]; then
    SPARROW_ASAN_OPTIONS="${SPARROW_ASAN_OPTIONS//detect_leaks=1/detect_leaks=0}"
fi

export ASAN_OPTIONS LSAN_OPTIONS TSAN_OPTIONS UBSAN_OPTIONS SPARROW_ASAN_OPTIONS

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-$$"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
fi

export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS=headless
    export WLR_HEADLESS_OUTPUTS=1
    export LIBGL_ALWAYS_SOFTWARE=1
    export SPARROW_NO_REALTIME=1
fi

mkdir -p "$OUT_DIR"
rm -f "$SPARROW_LOG" "$RUNNER_LOG"

SPARROW_PID=""
RUNNER_PID=""

cleanup() {
    if [ -n "$RUNNER_PID" ] && kill -0 "$RUNNER_PID" 2>/dev/null; then
        kill -TERM "$RUNNER_PID" 2>/dev/null || true
        wait "$RUNNER_PID" 2>/dev/null || true
    fi
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 3. Launch Sparrow Compositor
echo -e "\n${BLUE}[1/4] Starting Sparrow compositor...${NC}"
ASAN_OPTIONS="$SPARROW_ASAN_OPTIONS" "$SPARROW_BIN" --no-realtime > "$SPARROW_LOG" 2>&1 &
SPARROW_PID=$!

# Wait for Wayland socket to be ready
SPARROW_DISPLAY=""
RUNTIME_PATH="${XDG_RUNTIME_DIR:-/run/user/$UID}"
for _ in $(seq 1 80); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$SPARROW_LOG" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$SPARROW_LOG" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$RUNTIME_PATH/$SPARROW_DISPLAY" ]; then
            break
        fi
    fi
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [ -z "$SPARROW_DISPLAY" ] || [ ! -S "$RUNTIME_PATH/$SPARROW_DISPLAY" ]; then
    echo -e "${RED}[FAIL] Sparrow failed to initialize Wayland socket within 8s.${NC}"
    echo "Compositor Log:"
    cat "$SPARROW_LOG"
    exit 1
fi

echo -e "${GREEN}[OK] Sparrow running on ${CYAN}${SPARROW_DISPLAY}${GREEN} (PID: ${SPARROW_PID})${NC}"

# 4. Launch Sparrow App Runner
echo -e "\n${BLUE}[2/4] Starting sparrow-app-runner with application...${NC}"
RUNNER_ARGS=("$APP_BUNDLE")
if [ "$USE_VULKAN" = true ]; then
    RUNNER_ARGS+=("--vulkan")
    echo -e "${MAGENTA}[VULKAN] Launching runner with native Wayland Vulkan backend${NC}"
fi
if [ "$VK_VALIDATION" = true ]; then
    RUNNER_ARGS+=("--vk-validation")
    echo -e "${MAGENTA}[VULKAN] Khronos Validation Layers enabled${NC}"
fi
if [ -n "$VK_PRESENT_MODE" ]; then
    RUNNER_ARGS+=("--vk-present-mode=${VK_PRESENT_MODE}")
    echo -e "${MAGENTA}[VULKAN] Preferred Present Mode: ${VK_PRESENT_MODE}${NC}"
fi
WAYLAND_DISPLAY="$SPARROW_DISPLAY" "$RUNNER_BIN" "${RUNNER_ARGS[@]}" > "$RUNNER_LOG" 2>&1 &
RUNNER_PID=$!

RUNNER_CONNECTED=false
for _ in $(seq 1 50); do
    if grep -F "Connected to secure private socketpair" "$RUNNER_LOG" >/dev/null 2>&1; then
        RUNNER_CONNECTED=true
        break
    fi
    if ! kill -0 "$RUNNER_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [ "$RUNNER_CONNECTED" = true ]; then
    echo -e "${GREEN}[OK] Runner connected to Sparrow via Zero-Trust socketpair (PID: ${RUNNER_PID})${NC}"
else
    echo -e "${YELLOW}[WARNING] Zero-Trust socketpair handshake still pending or unconfirmed.${NC}"
fi

# 5. Inject Synthetic Inputs & UI Stress Test
echo -e "\n${BLUE}[3/4] Driving automated inputs and IPC requests (${DURATION_SEC}s)...${NC}"
START_TIME=$(date +%s)
LOOP_COUNT=0

while [ $(($(date +%s) - START_TIME)) -lt "$DURATION_SEC" ]; do
    LOOP_COUNT=$((LOOP_COUNT + 1))
    
    if ! kill -0 "$RUNNER_PID" 2>/dev/null; then
        echo -e "${RED}[ERROR] sparrow-app-runner exited prematurely!${NC}"
        break
    fi
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        echo -e "${RED}[ERROR] Sparrow compositor exited prematurely!${NC}"
        break
    fi

    # Synthetic pointer and keyboard inputs using wlrctl if available
    if command -v wlrctl &>/dev/null; then
        WAYLAND_DISPLAY="$SPARROW_DISPLAY" wlrctl pointer move 200 300 2>/dev/null || true
        WAYLAND_DISPLAY="$SPARROW_DISPLAY" wlrctl pointer click left 2>/dev/null || true
        WAYLAND_DISPLAY="$SPARROW_DISPLAY" wlrctl keyboard type "sparrow test input $LOOP_COUNT" 2>/dev/null || true
        WAYLAND_DISPLAY="$SPARROW_DISPLAY" wlrctl pointer move 400 500 2>/dev/null || true
        WAYLAND_DISPLAY="$SPARROW_DISPLAY" wlrctl pointer click left 2>/dev/null || true
    fi

    sleep 0.5
done

# 6. Graceful Shutdown
echo -e "\n${BLUE}[4/4] Shutting down runner and compositor gracefully...${NC}"

RUNNER_EXIT_CODE=0
if kill -0 "$RUNNER_PID" 2>/dev/null; then
    kill -TERM "$RUNNER_PID" 2>/dev/null || true
    wait "$RUNNER_PID" || RUNNER_EXIT_CODE=$?
fi

SPARROW_EXIT_CODE=0
if kill -0 "$SPARROW_PID" 2>/dev/null; then
    kill -TERM "$SPARROW_PID" 2>/dev/null || true
    wait "$SPARROW_PID" || SPARROW_EXIT_CODE=$?
fi

# 7. Analyze Logs for Sanitizer Errors & Leaks
check_errors() {
    local log="$1"
    local has_error=false

    # ASan checks
    if grep -Ei "(ERROR: AddressSanitizer|heap-use-after-free|heap-buffer-overflow|global-buffer-overflow|stack-use-after-return)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # LSan memory leaks
    if grep -Ei "(ERROR: LeakSanitizer: detected memory leaks)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # TSan data races
    if grep -Ei "(WARNING: ThreadSanitizer: data race)" "$log" >/dev/null 2>&1; then
        has_error=true
    fi
    # UBSan undefined behavior
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

SPARROW_STATUS=$(check_errors "$SPARROW_LOG")
RUNNER_STATUS=$(check_errors "$RUNNER_LOG")

IPC_HANDSHAKE=false
if grep -F "Created secure socketpair" "$SPARROW_LOG" >/dev/null 2>&1 && \
   grep -F "Connected to secure private socketpair" "$RUNNER_LOG" >/dev/null 2>&1; then
    IPC_HANDSHAKE=true
fi

IPC_CALLS=false
if grep -E "(MethodChannel \[sparrow/ipc\]|Pigeon IPC \[sparrow/ipc\])" "$RUNNER_LOG" >/dev/null 2>&1 && \
   grep -F "[sparrow-ipc] Request" "$SPARROW_LOG" >/dev/null 2>&1; then
    IPC_CALLS=true
fi

echo ""
echo -e "${BOLD}======================================================${NC}"
echo -e "${BOLD}                  TEST RESULTS SUMMARY                ${NC}"
echo -e "${BOLD}======================================================${NC}"
echo -e "Sparrow Exit Code : $([ $SPARROW_EXIT_CODE -eq 0 ] && echo -e "${GREEN}0 (SUCCESS)${NC}" || echo -e "${RED}${SPARROW_EXIT_CODE} (FAIL)${NC}")"
echo -e "Runner Exit Code  : $([ $RUNNER_EXIT_CODE -eq 0 ] && echo -e "${GREEN}0 (SUCCESS)${NC}" || echo -e "${RED}${RUNNER_EXIT_CODE} (FAIL)${NC}")"
echo -e "Sparrow Log Status: $([ "$SPARROW_STATUS" = "CLEAN" ] && echo -e "${GREEN}CLEAN (0 sanitizer errors / leaks)${NC}" || echo -e "${RED}ERROR / LEAK DETECTED${NC}")"
echo -e "Runner Log Status : $([ "$RUNNER_STATUS" = "CLEAN" ] && echo -e "${GREEN}CLEAN (0 sanitizer errors / leaks)${NC}" || echo -e "${RED}ERROR / LEAK DETECTED${NC}")"
echo -e "IPC Handshake     : $([ "$IPC_HANDSHAKE" = true ] && echo -e "${GREEN}VERIFIED (Anonymous kernel socketpair)${NC}" || echo -e "${YELLOW}NOT DETECTED${NC}")"
echo -e "IPC Method Calls  : $([ "$IPC_CALLS" = true ] && echo -e "${GREEN}VERIFIED (Pigeon IPC <-> Compositor Core)${NC}" || echo -e "${YELLOW}NOT DETECTED${NC}")"
echo -e "${BOLD}======================================================${NC}"

if [ "$SHOW_LOGS" = true ] || [ "$VERBOSE" = true ]; then
    echo -e "\n${BOLD}--- Compositor Log Output ---${NC}"
    cat "$SPARROW_LOG"
    echo -e "\n${BOLD}--- Runner Log Output ---${NC}"
    cat "$RUNNER_LOG"
fi

if [ $SPARROW_EXIT_CODE -eq 0 ] && [ $RUNNER_EXIT_CODE -eq 0 ] && \
   [ "$SPARROW_STATUS" = "CLEAN" ] && [ "$RUNNER_STATUS" = "CLEAN" ] && \
   [ "$IPC_HANDSHAKE" = true ]; then
    echo -e "\n${GREEN}${BOLD}✔ ALL TESTS PASSED SUCCESSFULLY!${NC}\n"
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ TEST FAILED! Examine logs in ${SPARROW_LOG} and ${RUNNER_LOG}.${NC}\n"
    echo -e "Tail of Runner Log:"
    tail -n 20 "$RUNNER_LOG"
    echo ""
    echo -e "Tail of Sparrow Log:"
    tail -n 20 "$SPARROW_LOG"
    exit 1
fi
