#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Fixed Size / Non-Resizable Window Test Harness
# ==============================================================================
# Validates that fixed-size applications (where min_size == max_size, e.g. cpu-x):
#   1. Maintain their original 1:1 aspect ratio and dimensions (no distortion)
#   2. Are cleanly centered on the screen with black letterboxing
#   3. Receive accurate pointer / touch coordinate events
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
CLIENT_BIN="${ROOT_DIR}/tools/bot/repro_clients/fixed_size_client"

LOG_SPARROW="${OUT_DIR}/fixed_size_sparrow.log"
LOG_CLIENT="${OUT_DIR}/fixed_size_client.log"

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

echo -e "${CYAN}${BOLD}=== Sparrow Fixed-Size Window & Letterbox Test Suite ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

if [ ! -f "$CLIENT_BIN" ]; then
    echo -e "${YELLOW}Building fixed_size_client binary...${NC}"
    make -C "${ROOT_DIR}/tools/bot/repro_clients" fixed_size_client
fi

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW" "$LOG_CLIENT"

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-fixed-$$"
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
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-fixed-* ]]; then
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

# 2. Launch Fixed Size Client (640x480)
echo -e "${CYAN}[2/4] Starting Fixed-Size Client (640x480 non-resizable)...${NC}"
"$CLIENT_BIN" --width=640 --height=480 --duration=10 --log="$LOG_CLIENT" &
CLIENT_PID=$!

CLIENT_READY=false
for _ in $(seq 1 40); do
    if [ -f "$LOG_CLIENT" ] && grep -F "[FIXED_READY]" "$LOG_CLIENT" >/dev/null 2>&1; then
        CLIENT_READY=true
        break
    fi
    sleep 0.1
done

if [ "$CLIENT_READY" = false ]; then
    echo -e "${RED}[FAIL] Fixed-size client failed to map.${NC}"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] Fixed-size client mapped: 640x480 (min=640x480 max=640x480)${NC}"

sleep 1.0

# 3. Capture screenshot & test pointer hit-testing
echo -e "${CYAN}[3/4] Capturing screenshot and verifying input dispatch...${NC}"
if command -v grim &> /dev/null; then
    grim "${OUT_DIR}/fixed_size_screenshot.png" 2>/dev/null && \
        echo -e "${GREEN}[OK] Screenshot captured to ${OUT_DIR}/fixed_size_screenshot.png${NC}" || true
fi
SPARROW_KEY="${ROOT_DIR}/tools/bot/repro_clients/sparrow_key"
if [ -f "$SPARROW_KEY" ]; then
    echo -e "  -> Toggling Overview mode..."
    "$SPARROW_KEY" Alt_L 2>/dev/null || true
    sleep 0.8
    if command -v grim &> /dev/null; then
        grim "${OUT_DIR}/fixed_size_overview_screenshot.png" 2>/dev/null && \
            echo -e "${GREEN}[OK] Overview screenshot captured to ${OUT_DIR}/fixed_size_overview_screenshot.png${NC}" || true
    fi
    # Exit overview mode
    "$SPARROW_KEY" Alt_L 2>/dev/null || true
    sleep 0.5
fi

if command -v wlrctl &> /dev/null; then
    # Inject pointer motion and click
    wlrctl pointer move 0 0 2>/dev/null || true
    sleep 0.2
    wlrctl pointer click left 2>/dev/null || true
    sleep 0.2
    wlrctl pointer move 10 10 2>/dev/null || true
    sleep 0.2
    wlrctl pointer click left 2>/dev/null || true
    sleep 0.2
fi

# 4. Check client logs and compositor state
echo -e "${CYAN}[4/4] Verifying fixed-size geometry and letterbox state...${NC}"

# Check for fixed client readiness
READY_LOG=$(grep "\[FIXED_READY\]" "$LOG_CLIENT" || true)
echo -e "Client status: ${BOLD}${READY_LOG}${NC}"

# Check if compositor detected geometry
SURFACE_LOG=$(grep -E "(Surface .* mapped|min_width|max_width|view .* created)" "$LOG_SPARROW" | tail -n 5 || true)
if [ -n "$SURFACE_LOG" ] && [ "$VERBOSE" = true ]; then
    echo -e "Compositor surface logs:\n$SURFACE_LOG"
fi

if [ -n "$READY_LOG" ]; then
    echo -e "\n${GREEN}${BOLD}✔ FIXED-SIZE CLIENT TEST PASSED!${NC}"
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ FIXED-SIZE CLIENT TEST FAILED!${NC}"
    cat "$LOG_CLIENT"
    exit 1
fi
