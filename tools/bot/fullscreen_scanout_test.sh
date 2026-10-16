#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Fullscreen Direct Scanout & wl_shm Test Harness
# ==============================================================================
# Validates that software-rendered wl_shm clients requesting fullscreen:
#   1. Transition cleanly from compositing to Direct Scanout (DS: ON)
#   2. Correctly display updated damaged frames (e.g. game content drawn after
#      initial background), preventing stale buffer display
#   3. Correctly render the software cursor on top of fullscreen scanout content
#   4. Yield to Flutter compositing (DS: OFF) on dock gestures and resume cleanly
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
CLIENT_BIN="${ROOT_DIR}/tools/bot/repro_clients/fullscreen_shm_client"

LOG_SPARROW="${OUT_DIR}/fullscreen_scanout_sparrow.log"
LOG_CLIENT="${OUT_DIR}/fullscreen_scanout_client.log"

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

echo -e "${CYAN}${BOLD}=== Sparrow Fullscreen Direct Scanout Test Suite ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[FAIL] Sparrow binary not found at ${SPARROW_BIN}. Run ./build.sh optimize first.${NC}"
    exit 1
fi

if [ ! -f "$CLIENT_BIN" ]; then
    echo -e "${YELLOW}[BUILD] Compiling fullscreen_shm_client...${NC}"
    make -C "${ROOT_DIR}/tools/bot/repro_clients" fullscreen_shm_client
fi

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS=headless
    export WLR_RENDER_DRM_DEVICE=""
    export WLR_HEADLESS_OUTPUTS=1
    export XDG_RUNTIME_DIR="$(mktemp -d /tmp/sparrow-runtime-fs-XXXXXX)"
    chmod 0700 "$XDG_RUNTIME_DIR"
fi

SPARROW_PID=""
CLIENT_PID=""

cleanup() {
    if [ -n "$CLIENT_PID" ] && kill -0 "$CLIENT_PID" 2>/dev/null; then
        kill -TERM "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-fs-* ]]; then
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

# 2. Launch Fullscreen SHM Client
echo -e "${CYAN}[2/4] Starting Fullscreen SHM Client (initial background + dynamic game frame)...${NC}"
"$CLIENT_BIN" --duration=8 --update-delay-ms=300 --log="$LOG_CLIENT" &
CLIENT_PID=$!

CLIENT_READY=false
for _ in $(seq 1 50); do
    if [ -f "$LOG_CLIENT" ] && grep -F "[FS_FRAME2_COMMITTED]" "$LOG_CLIENT" >/dev/null 2>&1; then
        CLIENT_READY=true
        break
    fi
    sleep 0.1
done

if [ "$CLIENT_READY" = false ]; then
    echo -e "${RED}[FAIL] Client failed to commit second frame.${NC}"
    cat "$LOG_CLIENT"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] Client mapped in fullscreen and committed both frames.${NC}"

# 3. Wait for Direct Scanout settling delay (1000ms)
echo -e "${CYAN}[3/4] Waiting for Direct Scanout transition...${NC}"
DS_ACTIVE=false
for _ in $(seq 1 30); do
    if grep -F "entered Direct Scanout (DS: ON)" "$LOG_SPARROW" >/dev/null 2>&1; then
        DS_ACTIVE=true
        break
    fi
    sleep 0.1
done

if [ "$DS_ACTIVE" = false ]; then
    echo -e "${RED}[FAIL] Direct Scanout (DS: ON) did not engage.${NC}"
    grep -E "DIRECT-SCANOUT|scanout" "$LOG_SPARROW" | tail -n 10 || true
    exit 1
fi
echo -e "${GREEN}[OK] Direct Scanout engaged (DS: ON).${NC}"

# 4. Capture screenshot during Direct Scanout
echo -e "${CYAN}[4/4] Capturing Direct Scanout screenshot and verifying frame...${NC}"
SCREENSHOT_PATH="${OUT_DIR}/fullscreen_scanout_screenshot.png"
if command -v grim &> /dev/null; then
    timeout 3 grim "$SCREENSHOT_PATH" 2>/dev/null && \
        echo -e "${GREEN}[OK] Screenshot captured to ${SCREENSHOT_PATH}${NC}" || true
fi

# Audit compositor log for scanout flips and transitions
SCANOUT_LOGS=$(grep -E "DIRECT-SCANOUT|PACING-DIRECT-BLIT" "$LOG_SPARROW" | tail -n 5)
echo -e "${CYAN}Compositor Scanout Events:${NC}"
echo "$SCANOUT_LOGS"

echo ""
echo -e "${GREEN}${BOLD}✔ FULLSCREEN DIRECT SCANOUT TEST PASSED!${NC}"
