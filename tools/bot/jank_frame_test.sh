#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Jank / Junk Frame & Pacing Automated Regression Test
# ==============================================================================
# Measures Wayland frame callback deltas, P50/P95/P99 frame time latencies,
# and dropped/delayed frames (>20ms, >33.3ms) under active UI load (PageView
# swipes, Overview zoom transitions, rapid pointer events).
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
SPARROW_KEY="${ROOT_DIR}/tools/bot/repro_clients/sparrow_key"
JANK_MONITOR="${ROOT_DIR}/tools/bot/repro_clients/jank_monitor"
LOG_SPARROW="${OUT_DIR}/jank_test_sparrow.log"
LOG_MONITOR="${OUT_DIR}/jank_monitor.log"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

HEADLESS=false
VERBOSE=false
DURATION=6
MAX_JANK_PCT=20.0

for arg in "$@"; do
    case "$arg" in
        --headless|-H)
            HEADLESS=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --duration=*|-d=*)
            DURATION="${arg#*=}"
            ;;
        --max-jank=*|-j=*)
            MAX_JANK_PCT="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [--headless|-H] [--verbose|-v] [--duration=SEC] [--max-jank=PCT]"
            exit 0
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow Jank / Frame Pacing Regression Test ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

if ! command -v wlrctl &> /dev/null; then
    echo -e "${RED}[ERROR] 'wlrctl' command not found.${NC}"
    exit 1
fi

# Ensure jank monitor is compiled
make -C "${ROOT_DIR}/tools/bot/repro_clients" jank_monitor >/dev/null 2>&1

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW" "$LOG_MONITOR"

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-jank-$$"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
fi
export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"
export mesa_glthread=false

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS="headless"
    export WLR_HEADLESS_OUTPUTS="1"
fi

if [ -z "$WLR_RENDER_DRM_DEVICE" ]; then
    for node in /dev/dri/renderD*; do
        if [ -e "$node" ] && head -c 0 "$node" 2>/dev/null; then
            export WLR_RENDER_DRM_DEVICE="$node"
            break
        fi
    done
fi

SPARROW_PID=""
MONITOR_PID=""

cleanup() {
    if [ -n "$MONITOR_PID" ] && kill -0 "$MONITOR_PID" 2>/dev/null; then
        kill -TERM "$MONITOR_PID" 2>/dev/null || true
        wait "$MONITOR_PID" 2>/dev/null || true
    fi
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-jank-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Start Sparrow with FPS monitoring enabled
echo -e "${CYAN}[1/4] Launching Sparrow with FPS diagnostics...${NC}"
"$SPARROW_BIN" --no-realtime --fps > "$LOG_SPARROW" 2>&1 &
SPARROW_PID=$!

SPARROW_DISPLAY=""
RUNTIME_PATH="${XDG_RUNTIME_DIR:-/run/user/$UID}"
for _ in $(seq 1 60); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$LOG_SPARROW" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$LOG_SPARROW" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$RUNTIME_PATH/$SPARROW_DISPLAY" ]; then
            break
        fi
    fi
    sleep 0.1
done

if [ -z "$SPARROW_DISPLAY" ]; then
    echo -e "${RED}[FAIL] Sparrow failed to initialize socket.${NC}"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] Compositor active on ${SPARROW_DISPLAY}${NC}"

export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

# 2. Launch Jank Monitor client
echo -e "${CYAN}[2/4] Starting Frame Pacing & Jank Monitor (${DURATION}s, Max Jank: ${MAX_JANK_PCT}%)...${NC}"
"$JANK_MONITOR" --duration="$DURATION" --max-jank-pct="$MAX_JANK_PCT" > "$LOG_MONITOR" 2>&1 &
MONITOR_PID=$!

sleep 0.5

# 3. Inject synthetic stress while jank monitor records
echo -e "${CYAN}[3/4] Injecting synthetic UI load (swipes, cursor jitter, overview zoom)...${NC}"
START_TS=$(date +%s)

while kill -0 "$MONITOR_PID" 2>/dev/null; do
    # Pointer motions
    for x in 150 450 750 1050 600 200; do
        wlrctl pointer move "$x" 300 2>/dev/null || true
        usleep 25000 2>/dev/null || sleep 0.025
    done

    # Switch pages (stresses Flutter PageView rendering)
    "$SPARROW_KEY" combo Control_L Right 2>/dev/null || true
    sleep 0.2
    "$SPARROW_KEY" combo Control_L Left 2>/dev/null || true
    sleep 0.2

    # Toggle Overview mode (stresses damage clipping and scaled subsurfaces)
    "$SPARROW_KEY" Alt_L 2>/dev/null || true
    sleep 0.3
    "$SPARROW_KEY" Return 2>/dev/null || true
    sleep 0.3

    if [ $(($(date +%s) - START_TS)) -ge $((DURATION + 2)) ]; then
        break
    fi
done

# Wait for jank monitor completion
MONITOR_EXIT=0
wait "$MONITOR_PID" || MONITOR_EXIT=$?
MONITOR_PID=""

# 4. Graceful Shutdown & Validation
echo -e "${CYAN}[4/4] Shutting down compositor and evaluating metrics...${NC}"
kill -TERM "$SPARROW_PID"
EXIT_CODE=0
wait "$SPARROW_PID" || EXIT_CODE=$?
SPARROW_PID=""

cat "$LOG_MONITOR"

CRASH=false
if grep -Ei "(SIGSEGV|corrupted size|Segmentation fault|heap-use-after-free|double free)" "$LOG_SPARROW" > /dev/null; then
    CRASH=true
fi

echo ""
echo -e "${BOLD}--- Test Results ---${NC}"
echo -e "Jank Monitor Result : $([ $MONITOR_EXIT -eq 0 ] && echo "${GREEN}PASS${NC}" || echo "${RED}FAIL (Exit: $MONITOR_EXIT)${NC}")"
echo -e "Compositor Exit Code: $([ $EXIT_CODE -eq 0 ] && echo "${GREEN}0 (SUCCESS)${NC}" || echo "${RED}$EXIT_CODE (FAIL)${NC}")"
echo -e "Compositor Stability: $([ "$CRASH" = false ] && echo "${GREEN}STABLE${NC}" || echo "${RED}CRASH DETECTED${NC}")"

if [ $MONITOR_EXIT -eq 0 ] && [ $EXIT_CODE -eq 0 ] && [ "$CRASH" = false ]; then
    echo -e "\n${GREEN}${BOLD}✔ JANK & FRAME PACING REGRESSION TEST PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ JANK REGRESSION TEST FAILED!${NC}\n"
    exit 1
fi
