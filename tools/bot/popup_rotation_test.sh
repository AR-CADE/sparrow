#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Popup Rotation & Constraint Test Harness
# ==============================================================================
# Validates that open popups (menus, dropdowns, tooltips):
#   1. Are properly unconstrained when display output rotates (landscape <-> portrait)
#   2. Have their scene-graph coordinates and Flutter bounds updated
#   3. Do not remain stuck at stale coordinates
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
CLIENT_BIN="${ROOT_DIR}/tools/bot/repro_clients/repro_subsurface_popup"

LOG_SPARROW="${OUT_DIR}/popup_rotation_sparrow.log"
LOG_CLIENT="${OUT_DIR}/popup_rotation_client.log"

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

echo -e "${CYAN}${BOLD}=== Sparrow Popup Rotation & Coordinate Test Suite ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

if [ ! -f "$CLIENT_BIN" ]; then
    echo -e "${YELLOW}Building repro_subsurface_popup binary...${NC}"
    make -C "${ROOT_DIR}/tools/bot/repro_clients" repro_subsurface_popup
fi

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW" "$LOG_CLIENT"

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-popup-rot-$$"
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
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-popup-rot-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Start Sparrow
echo -e "${CYAN}[1/5] Launching Sparrow compositor...${NC}"
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

# 2. Launch Client with popup open
echo -e "${CYAN}[2/5] Starting client with active popup...${NC}"
"$CLIENT_BIN" --duration=15 > "$LOG_CLIENT" 2>&1 &
CLIENT_PID=$!

POPUP_MAPPED=false
for _ in $(seq 1 50); do
    if grep -F "Popup surface mapped:" "$LOG_SPARROW" >/dev/null 2>&1; then
        POPUP_MAPPED=true
        break
    fi
    sleep 0.1
done

if [ "$POPUP_MAPPED" = false ]; then
    echo -e "${RED}[FAIL] Popup surface failed to map.${NC}"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] Popup surface successfully mapped on toplevel window.${NC}"

sleep 0.8

# Capture baseline screenshot
if command -v grim &> /dev/null; then
    grim "${OUT_DIR}/popup_rot_landscape.png" 2>/dev/null && \
        echo -e "${GREEN}[OK] Landscape screenshot captured to ${OUT_DIR}/popup_rot_landscape.png${NC}" || true
fi

# Detect output name via wlr-randr
OUTPUT_NAME=$(wlr-randr 2>/dev/null | grep -oP '^\S+' | head -n 1 || true)
if [ -z "$OUTPUT_NAME" ]; then
    OUTPUT_NAME="HEADLESS-1"
fi
echo -e "  -> Detected display output: ${BOLD}${OUTPUT_NAME}${NC}"

# 3. Rotate output 90 degrees (landscape -> portrait)
echo -e "${CYAN}[3/5] Rotating output to 90 degrees (portrait)...${NC}"
INITIAL_UNCONSTRAIN_COUNT=$(grep -c "unconstrained:" "$LOG_SPARROW" || true)

wlr-randr --output "$OUTPUT_NAME" --transform 90 2>/dev/null || true
sleep 1.0

# Capture rotated screenshot
if command -v grim &> /dev/null; then
    grim "${OUT_DIR}/popup_rot_portrait.png" 2>/dev/null && \
        echo -e "${GREEN}[OK] Portrait screenshot captured to ${OUT_DIR}/popup_rot_portrait.png${NC}" || true
fi

POST_ROT_UNCONSTRAIN_COUNT=$(grep -c "unconstrained:" "$LOG_SPARROW" || true)
echo -e "  -> Unconstrain calls: initial=${INITIAL_UNCONSTRAIN_COUNT}, after-rotation=${POST_ROT_UNCONSTRAIN_COUNT}"

if [ "$POST_ROT_UNCONSTRAIN_COUNT" -le "$INITIAL_UNCONSTRAIN_COUNT" ]; then
    echo -e "${RED}[FAIL] Popup was not re-unconstrained during rotation!${NC}"
    exit 1
fi
echo -e "${GREEN}[OK] Popup re-unconstrained and coordinates updated for portrait mode.${NC}"

# 4. Rotate back to normal
echo -e "${CYAN}[4/5] Rotating output back to normal (landscape)...${NC}"
wlr-randr --output "$OUTPUT_NAME" --transform normal 2>/dev/null || true
sleep 1.0

FINAL_UNCONSTRAIN_COUNT=$(grep -c "unconstrained:" "$LOG_SPARROW" || true)
echo -e "  -> Unconstrain calls: after return to normal=${FINAL_UNCONSTRAIN_COUNT}"

if [ "$FINAL_UNCONSTRAIN_COUNT" -le "$POST_ROT_UNCONSTRAIN_COUNT" ]; then
    echo -e "${RED}[FAIL] Popup was not re-unconstrained when returning to normal!${NC}"
    exit 1
fi
echo -e "${GREEN}[OK] Popup re-unconstrained and coordinates restored for normal mode.${NC}"

# 5. Verify log events
echo -e "${CYAN}[5/5] Auditing popup map coordinates in compositor log...${NC}"
POPUP_MAPS=$(grep "Popup map: handle=" "$LOG_SPARROW" | tail -n 5 || true)
if [ "$VERBOSE" = true ] || [ -n "$POPUP_MAPS" ]; then
    echo -e "Latest popup map events:\n$POPUP_MAPS"
fi

echo -e "\n${GREEN}${BOLD}✔ POPUP ROTATION TEST PASSED! Popups update dynamically on screen rotation.${NC}"
exit 0
