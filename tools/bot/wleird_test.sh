#!/bin/bash
# ==============================================================================
# Sparrow - Wleird Comprehensive Stress Test & Sanitizer Harness
# ==============================================================================
# Runs the 'wleird' Wayland torture-test suite against Sparrow compositor.
# Covers all permutations of arguments, damage patterns, scale loops & modes.
# Detects crashes, ASan memory errors, UBSan undefined behaviors, and TSan races.
# ==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"
WLEIRD_DIR="${OUT_DIR}/wleird"
SPARROW_BIN="${OUT_DIR}/sparrow"
LOG_FILE="${OUT_DIR}/wleird_test.log"

DURATION_PER_TEST=5
THIEF_DURATION=45
FILTER=""
HEADLESS=false
SHOW_LOG=false
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        --duration=*|-d=*)
            DURATION_PER_TEST="${arg#*=}"
            ;;
        --thief-duration=*|-t=*)
            THIEF_DURATION="${arg#*=}"
            ;;
        --filter=*|-f=*)
            FILTER="${arg#*=}"
            ;;
        --headless|-H)
            HEADLESS=true
            ;;
        --show-log|-l)
            SHOW_LOG=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Usage: ./tools/bot/wleird_test.sh [OPTIONS...]"
            echo ""
            echo "Options:"
            echo "  --duration=N, -d=N        Duration in seconds for standard tests (default: 5s)"
            echo "  --thief-duration=N, -t=N  Duration in seconds for resource-thief tests (default: 45s)"
            echo "  --filter=NAME, -f=        Run only tests matching NAME (e.g. damage-paint, thief, unmap)"
            echo "  --headless, -H            Run in headless mode (WLR_BACKENDS=headless)"
            echo "  --show-log, -l            Print the complete log file at the end of execution"
            echo "  --verbose, -v             Stream live compositor log output during tests"
            echo "  --help, -h                Show this help message"
            echo ""
            echo "Filter Examples:"
            echo "  ./tools/bot/wleird_test.sh --filter=damage-paint      # Run all 13 damage tracking patterns"
            echo "  ./tools/bot/wleird_test.sh --filter=attach-delta-loop # Run scale sweep loop (1 to 1000)"
            echo "  ./tools/bot/wleird_test.sh --filter=resource-thief    # Run shmpool, dmabuf, and region starvation"
            echo "  ./tools/bot/wleird_test.sh --filter=copy-fu           # Run all 10 clipboard attack modes"
            echo "  ./tools/bot/wleird_test.sh --filter=unmap             # Run rapid unmap torture"
            exit 0
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow Wleird Comprehensive Torture Test Suite ===${NC}"
echo -e "Compositor Log : ${BOLD}${LOG_FILE}${NC}"

# 1. Prerequisite checks
if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    echo "Run ./build.sh (or ./build.sh server) first."
    exit 1
fi

ACTIVE_SANITIZER="None (Release / Standard Debug)"
if nm "$SPARROW_BIN" 2>/dev/null | grep -q "__asan_init"; then
    ACTIVE_SANITIZER="AddressSanitizer (ASan)"
elif nm "$SPARROW_BIN" 2>/dev/null | grep -q "__tsan_init"; then
    ACTIVE_SANITIZER="ThreadSanitizer (TSan)"
elif nm "$SPARROW_BIN" 2>/dev/null | grep -q "__ubsan_handle"; then
    ACTIVE_SANITIZER="UndefinedBehaviorSanitizer (UBSan)"
fi
echo -e "Sanitizer Mode : ${BOLD}${ACTIVE_SANITIZER}${NC}"

# Auto-configure standard suppressions if not already specified by the user
if [ -f "${ROOT_DIR}/lsan_suppressions.txt" ]; then
    if [ -z "$LSAN_OPTIONS" ]; then
        export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt"
    elif [[ "$LSAN_OPTIONS" != *"suppressions="* ]]; then
        export LSAN_OPTIONS="suppressions=${ROOT_DIR}/lsan_suppressions.txt:${LSAN_OPTIONS}"
    fi
fi

if [ -f "${ROOT_DIR}/tsan_suppressions.txt" ]; then
    if [ -z "$TSAN_OPTIONS" ]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:report_signal_unsafe=0:second_deadlock_stack=1"
    elif [[ "$TSAN_OPTIONS" != *"suppressions="* ]]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:report_signal_unsafe=0:second_deadlock_stack=1:${TSAN_OPTIONS}"
    fi
fi

if [ -f "${ROOT_DIR}/ubsan_suppressions.txt" ]; then
    if [ -z "$UBSAN_OPTIONS" ]; then
        export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt"
    elif [[ "$UBSAN_OPTIONS" != *"suppressions="* ]]; then
        export UBSAN_OPTIONS="suppressions=${ROOT_DIR}/ubsan_suppressions.txt:${UBSAN_OPTIONS}"
    fi
fi

if [ -n "$ASAN_OPTIONS" ]; then
    echo -e "ASAN_OPTIONS   : ${BOLD}${ASAN_OPTIONS}${NC}"
fi
if [ -n "$LSAN_OPTIONS" ]; then
    echo -e "LSAN_OPTIONS   : ${BOLD}${LSAN_OPTIONS}${NC}"
fi
if [ -n "$UBSAN_OPTIONS" ]; then
    echo -e "UBSAN_OPTIONS  : ${BOLD}${UBSAN_OPTIONS}${NC}"
fi
if [ -n "$TSAN_OPTIONS" ]; then
    echo -e "TSAN_OPTIONS   : ${BOLD}${TSAN_OPTIONS}${NC}"
fi
export ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS

if [ ! -d "$WLEIRD_DIR" ] || [ -z "$(ls -A "$WLEIRD_DIR" 2>/dev/null)" ]; then
    echo -e "${RED}[ERROR] Wleird test binaries not found at ${WLEIRD_DIR}.${NC}"
    echo "Run ./build.sh server to build and install wleird targets."
    exit 1
fi

# Setup runtime dir and library path
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

rm -f "$LOG_FILE"

# 2. Launch Sparrow
echo -e "\n${BLUE}[1/2] Starting Sparrow compositor...${NC}"
"$SPARROW_BIN" --no-realtime > "$LOG_FILE" 2>&1 &
SPARROW_PID=$!

cleanup() {
    if [ -n "$TAIL_PID" ] && kill -0 "$TAIL_PID" 2>/dev/null; then
        kill -TERM "$TAIL_PID" 2>/dev/null || true
    fi
    if kill -0 "$SPARROW_PID" 2>/dev/null; then
        echo -e "\n${BLUE}Stopping Sparrow compositor (PID: $SPARROW_PID)...${NC}"
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [ -f "$LOG_FILE" ] && grep -qE "SUMMARY: (AddressSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer)" "$LOG_FILE" 2>/dev/null; then
        echo -e "\n${RED}${BOLD}=== Sanitizer Report Detected ===${NC}"
        grep -E "SUMMARY: (AddressSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer)" "$LOG_FILE"
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
    if [ "$SHOW_LOG" = true ] && [ -f "$LOG_FILE" ]; then
        echo -e "\n${BOLD}=== Full Compositor Log (${LOG_FILE}) ===${NC}"
        cat "$LOG_FILE"
    fi
}
trap cleanup EXIT INT TERM

# Wait for socket and parse WAYLAND_DISPLAY
SPARROW_DISPLAY=""
RUNTIME_PATH="${XDG_RUNTIME_DIR:-/run/user/$UID}"
for _ in $(seq 1 60); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$LOG_FILE" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$LOG_FILE" | head -n 1)
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
    echo -e "${RED}[FAIL] Sparrow failed to initialize Wayland socket.${NC}"
    echo "Log output:"
    cat "$LOG_FILE"
    exit 1
fi

echo -e "${GREEN}Sparrow active on WAYLAND_DISPLAY=${SPARROW_DISPLAY}${NC}\n"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

if [ "$VERBOSE" = true ]; then
    tail -f "$LOG_FILE" &
    TAIL_PID=$!
fi

# 3. Test Matrix Definition
# Format: binary_name:arguments:display_label
TEST_MATRIX=(
    # --- wleird-damage-paint: all 13 damage patterns ---
    "wleird-damage-paint:blocknormal:damage-paint (blocknormal)"
    "wleird-damage-paint:circle:damage-paint (circle)"
    "wleird-damage-paint:endpoints:damage-paint (endpoints)"
    "wleird-damage-paint:fat-grid:damage-paint (fat-grid)"
    "wleird-damage-paint:fat-grid-h:damage-paint (fat-grid-h)"
    "wleird-damage-paint:fine-grid:damage-paint (fine-grid)"
    "wleird-damage-paint:normal:damage-paint (normal)"
    "wleird-damage-paint:overcopy:damage-paint (overcopy)"
    "wleird-damage-paint:vstack:damage-paint (vstack)"
    "wleird-damage-paint:ring:damage-paint (ring)"
    "wleird-damage-paint:snow:damage-paint (snow)"
    "wleird-damage-paint:snow2:damage-paint (snow2)"
    "wleird-damage-paint:wraparound:damage-paint (wraparound)"

    # --- wleird-disobey-resize: varying scale factors ---
    "wleird-disobey-resize:0.1:disobey-resize (factor=0.1)"
    "wleird-disobey-resize:0.5:disobey-resize (factor=0.5)"
    "wleird-disobey-resize:0.8:disobey-resize (factor=0.8)"
    "wleird-disobey-resize:1.2:disobey-resize (factor=1.2)"
    "wleird-disobey-resize:1.5:disobey-resize (factor=1.5)"
    "wleird-disobey-resize:2.0:disobey-resize (factor=2.0)"
    "wleird-disobey-resize:5.0:disobey-resize (factor=5.0)"
    "wleird-disobey-resize:10.0:disobey-resize (factor=10.0)"

    # --- wleird-resource-thief: all 3 resource starvation modes ---
    "wleird-resource-thief:shmpool:resource-thief (shmpool)"
    "wleird-resource-thief:dmabuf:resource-thief (dmabuf)"
    "wleird-resource-thief:region:resource-thief (region)"

    # --- wleird-copy-fu: all 10 clipboard attack modes ---
    "wleird-copy-fu:default:copy-fu (default)"
    "wleird-copy-fu:cat-rand:copy-fu (cat-rand)"
    "wleird-copy-fu:bad-serial:copy-fu (bad-serial)"
    "wleird-copy-fu:steal-serial:copy-fu (steal-serial)"
    "wleird-copy-fu:zero-sink:copy-fu (zero-sink)"
    "wleird-copy-fu:recv-file:copy-fu (recv-file)"
    "wleird-copy-fu:steal-sync:copy-fu (steal-sync)"
    "wleird-copy-fu:recv-flood:copy-fu (recv-flood)"
    "wleird-copy-fu:recv-sockpair:copy-fu (recv-sockpair)"
    "wleird-copy-fu:recv-epipe:copy-fu (recv-epipe)"

    # --- wleird-attach-delta-loop: sweep scales from 1 to 1000 ---
    "wleird-attach-delta-loop:1:attach-delta-loop (scale=1)"
    "wleird-attach-delta-loop:5:attach-delta-loop (scale=5)"
    "wleird-attach-delta-loop:20:attach-delta-loop (scale=20 [default])"
    "wleird-attach-delta-loop:50:attach-delta-loop (scale=50)"
    "wleird-attach-delta-loop:100:attach-delta-loop (scale=100)"
    "wleird-attach-delta-loop:250:attach-delta-loop (scale=250)"
    "wleird-attach-delta-loop:500:attach-delta-loop (scale=500)"
    "wleird-attach-delta-loop:750:attach-delta-loop (scale=750)"
    "wleird-attach-delta-loop:1000:attach-delta-loop (scale=1000)"

    # --- Standard clients without required arguments ---
    "wleird-cursor::cursor"
    "wleird-frame-callback::frame-callback"
    "wleird-gamma-blend::gamma-blend"
    "wleird-resize-loop::resize-loop"
    "wleird-resizor::resizor"
    "wleird-sigbus::sigbus"
    "wleird-slow-ack-configure::slow-ack-configure"
    "wleird-subsurfaces::subsurfaces"
    "wleird-surface-outputs::surface-outputs"
    "wleird-unmap::unmap"
)

# 4. Discover and run test matrix
echo -e "${BLUE}[2/2] Executing Wleird Test Matrix...${NC}"

PASSED_COUNT=0
FAILED_COUNT=0
TOTAL_COUNT=0

for entry in "${TEST_MATRIX[@]}"; do
    IFS=':' read -r bin_name bin_args test_label <<< "$entry"
    test_bin="${WLEIRD_DIR}/${bin_name}"

    if [ ! -f "$test_bin" ]; then
        continue
    fi

    # Filter by name, arg, or label
    if [ -n "$FILTER" ]; then
        if [[ "$bin_name" != *"$FILTER"* ]] && [[ "$bin_args" != *"$FILTER"* ]] && [[ "$test_label" != *"$FILTER"* ]]; then
            continue
        fi
    fi

    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    effective_duration="$DURATION_PER_TEST"
    if [[ "$bin_name" == *"resource-thief"* ]]; then
        effective_duration="$THIEF_DURATION"
    fi
    printf "  ${CYAN}${BOLD}%-42s${NC} (%ss)... " "${test_label}" "${effective_duration}"

    # Launch client (disowned so client-side SIGBUS/SIGSEGV won't leak bash job messages)
    if [ -n "$bin_args" ]; then
        "$test_bin" $bin_args > /dev/null 2>&1 &
    else
        "$test_bin" > /dev/null 2>&1 &
    fi
    CLIENT_PID=$!
    disown "$CLIENT_PID" 2>/dev/null || true

    # Monitor Sparrow health during the duration (early exit if client finishes on its own)
    SPARROW_CRASHED=false
    for _ in $(seq 1 $((effective_duration * 10))); do
        if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
            SPARROW_CRASHED=true
            break
        fi
        if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    # Terminate client
    if kill -0 "$CLIENT_PID" 2>/dev/null; then
        kill -TERM "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi

    if [ "$SPARROW_CRASHED" = true ]; then
        echo -e "${RED}${BOLD}[CRASH DETECTED]${NC}"
        echo -e "${RED}Sparrow crashed while running ${test_label}!${NC}"
        echo -e "\nLog output:"
        tail -n 50 "$LOG_FILE"
        FAILED_COUNT=$((FAILED_COUNT + 1))
        exit 1
    else
        echo -e "${GREEN}SURVIVED${NC}"
        PASSED_COUNT=$((PASSED_COUNT + 1))
    fi
done

echo -e "\n${BOLD}=== Wleird Test Results ===${NC}"
echo -e "Passed         : ${GREEN}${PASSED_COUNT}/${TOTAL_COUNT}${NC}"
echo -e "Compositor Log : ${BOLD}${LOG_FILE}${NC}"
if [ "$FAILED_COUNT" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}All tested permutations survived without compositor crashes!${NC}"
    exit 0
else
    echo -e "${RED}${BOLD}${FAILED_COUNT} test permutations triggered a compositor failure!${NC}"
    exit 1
fi
