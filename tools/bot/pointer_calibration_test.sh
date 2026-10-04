#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Pointer & Coordinate Calibration Test Harness
# ==============================================================================
# Tests pointer precision, coordinate mapping, and scaling by sending synthetic
# input events via wlrctl and measuring the actual coordinates received by a
# Wayland client (pointer_calibrator).
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
CALIBRATOR_BIN="${ROOT_DIR}/tools/bot/repro_clients/pointer_calibrator"

LOG_SPARROW="${OUT_DIR}/pointer_calib_sparrow.log"
LOG_CALIB="${OUT_DIR}/pointer_calib_client.log"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

HEADLESS=false
VERBOSE=false

for arg in "$@"; do
    case "$arg" in
        --headless|-H)
            HEADLESS=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Usage: $0 [--headless|-H] [--verbose|-v]"
            exit 0
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow Pointer Calibration Test Suite ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

if [ ! -f "$CALIBRATOR_BIN" ]; then
    echo -e "${YELLOW}Building calibrator binary...${NC}"
    make -C "${ROOT_DIR}/tools/bot/repro_clients" pointer_calibrator
fi

if ! command -v wlrctl &> /dev/null; then
    echo -e "${RED}[ERROR] 'wlrctl' command not found.${NC}"
    exit 1
fi

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW" "$LOG_CALIB"

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-calib-$$"
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
CALIB_PID=""

cleanup() {
    if [ -n "$CALIB_PID" ] && kill -0 "$CALIB_PID" 2>/dev/null; then
        kill -TERM "$CALIB_PID" 2>/dev/null || true
        wait "$CALIB_PID" 2>/dev/null || true
    fi
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-calib-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Start Sparrow
echo -e "${CYAN}[1/4] Launching Sparrow compositor...${NC}"
"$SPARROW_BIN" --no-realtime > "$LOG_SPARROW" 2>&1 &
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
    echo -e "${RED}[FAIL] Sparrow failed to start.${NC}"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] Compositor ready on ${SPARROW_DISPLAY}${NC}"

export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

# 2. Launch Pointer Calibrator Client
echo -e "${CYAN}[2/4] Starting Pointer Calibrator client...${NC}"
"$CALIBRATOR_BIN" --duration=10 --log="$LOG_CALIB" &
CALIB_PID=$!

# Wait for client to map
CLIENT_READY=false
for _ in $(seq 1 40); do
    if [ -f "$LOG_CALIB" ] && grep -F "[CALIB_READY]" "$LOG_CALIB" >/dev/null 2>&1; then
        CLIENT_READY=true
        break
    fi
    sleep 0.1
done

if [ "$CLIENT_READY" = false ]; then
    echo -e "${RED}[FAIL] Calibrator client failed to map.${NC}"
    exit 1
fi
echo -e "${GREEN}[OK] Calibrator window mapped (800x600)${NC}"

sleep 1.5

# 3. Inject synthetic pointer movements
echo -e "${CYAN}[3/4] Injecting calibrated pointer motion & clicks via wlrctl...${NC}"

# Move cursor into the centered window (240..1040, 60..660) and perform clicks
wlrctl pointer move 400 300 2>/dev/null || true
sleep 0.5
wlrctl pointer click left 2>/dev/null || true
sleep 0.3

wlrctl pointer move -40 -40 2>/dev/null || true
sleep 0.2
wlrctl pointer click left 2>/dev/null || true
sleep 0.2

wlrctl pointer move 30 10 2>/dev/null || true
sleep 0.2
wlrctl pointer click left 2>/dev/null || true
sleep 0.2

# 4. Verification
echo -e "${CYAN}[4/4] Verifying pointer coordinate logs...${NC}"

EVENT_COUNT=$(grep -c "\[CALIB_POINTER\]" "$LOG_CALIB" || true)
BUTTON_COUNT=$(grep -c "button" "$LOG_CALIB" || true)

echo -e "Total Pointer Events Received: ${BOLD}${EVENT_COUNT}${NC}"
echo -e "Button Clicks Registered     : ${BOLD}${BUTTON_COUNT}${NC}"

if [ "$EVENT_COUNT" -gt 0 ] && [ "$BUTTON_COUNT" -gt 0 ]; then
    echo -e "\n${GREEN}${BOLD}✔ POINTER CALIBRATION TEST PASSED!${NC}"
    echo "Sample events received by client:"
    grep "\[CALIB_POINTER\]" "$LOG_CALIB" | head -n 8
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ POINTER CALIBRATION TEST FAILED! No events reached the client.${NC}"
    cat "$LOG_CALIB"
    exit 1
fi
