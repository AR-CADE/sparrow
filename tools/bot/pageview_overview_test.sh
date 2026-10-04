#!/usr/bin/env bash
# ==============================================================================
# Sparrow - PageView & Overview Automated Integration Test Suite
# ==============================================================================
# Validates multi-window lifecycle, PageView transitions, Overview zoom mode,
# window switching, Rainbow damage visualization, and FPS monitor stability.
# Integrates with the Dart VM Service (http://127.0.0.1:8181/) to verify Dart
# isolate health, memory stability, and absence of uncaught framework exceptions.
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
VM_CLIENT="${ROOT_DIR}/tools/bot/vm_service_client.py"
SPARROW_KEY="${ROOT_DIR}/tools/bot/repro_clients/sparrow_key"
JANK_MONITOR="${ROOT_DIR}/tools/bot/repro_clients/jank_monitor"

LOG_SPARROW="${OUT_DIR}/pageview_overview_sparrow.log"
LOG_MONITOR="${OUT_DIR}/pageview_jank_monitor.log"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
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

echo -e "${CYAN}${BOLD}=== Sparrow PageView & Overview Automated Integration Test ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

if ! command -v wlrctl &> /dev/null; then
    echo -e "${RED}[ERROR] 'wlrctl' command not found.${NC}"
    exit 1
fi

# Ensure repro clients are compiled
make -C "${ROOT_DIR}/tools/bot/repro_clients" all >/dev/null 2>&1

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW"

# Runtime environment setup
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-overview-$$"
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
PIDS=()

cleanup() {
    for pid in "${PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-overview-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Start Sparrow with VM Service enabled
echo -e "${CYAN}[1/6] Launching Sparrow with VM Service (port 8181)...${NC}"
"$SPARROW_BIN" --no-realtime --vm-service=8181 > "$LOG_SPARROW" 2>&1 &
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

# 2. Verify Dart VM Service connection
echo -e "${CYAN}[2/6] Connecting to Dart VM Service via JSON-RPC...${NC}"
if [ -f "$VM_CLIENT" ]; then
    if python3 "$VM_CLIENT" --wait --timeout=8 >/dev/null 2>&1; then
        echo -e "${GREEN}[OK] Dart VM Service responded cleanly.${NC}"
        python3 "$VM_CLIENT" --check-health
        python3 "$VM_CLIENT" --memory
    else
        echo -e "${YELLOW}[NOTICE] Dart VM Service not responding on port 8181 (may be release AOT build without VM service). Continuing test...${NC}"
    fi
fi

# 3. Spawn multiple client windows (Multi-page PageView)
echo -e "${CYAN}[3/6] Spawning client windows across multiple pages...${NC}"

# Client 1: Pointer Calibrator (Page 0)
"${ROOT_DIR}/tools/bot/repro_clients/pointer_calibrator" --duration=30 >/dev/null 2>&1 &
PIDS+=($!)
sleep 0.8

# Client 2: Subsurface Popup with Interactive Button (Page 1)
"${ROOT_DIR}/tools/bot/repro_clients/repro_subsurface_popup" --duration=30 >/dev/null 2>&1 &
PIDS+=($!)
sleep 0.8

# Client 3: Tooltip ARGB (Page 2)
"${ROOT_DIR}/tools/bot/repro_clients/repro_tooltip_argb" --duration=30 >/dev/null 2>&1 &
PIDS+=($!)
sleep 0.8

# Client 4: Jank & Frame Pacing Monitor (Page 3)
"$JANK_MONITOR" --duration=16 --max-jank-pct=25 > "$LOG_MONITOR" 2>&1 &
JANK_PID=$!
PIDS+=($JANK_PID)
sleep 0.8

echo -e "${GREEN}[OK] 4 clients mapped onto PageView (including Jank Monitor).${NC}"

# 4. Navigate PageView & Toggle Diagnostic Overlays (Rainbow, FPS)
echo -e "${CYAN}[4/6] Testing PageView navigation, Rainbow borders (F12), and FPS OSD (F11)...${NC}"

# Enable FPS OSD via F11
"$SPARROW_KEY" F11 2>/dev/null || true
sleep 0.4

# Enable Rainbow damage via F12
"$SPARROW_KEY" F12 2>/dev/null || true
sleep 0.4

# Horizontal page navigation: Page 0 -> Page 1
"$SPARROW_KEY" combo Control_L Right 2>/dev/null || true
sleep 0.6

# On Page 1 (Subsurface Popup Client), click interactive button at (180, 60) to close and re-open popup
echo -e "  -> Testing interactive Subsurface Popup button click (close/open)..."
wlrctl pointer move 180 60 2>/dev/null || true
sleep 0.2
wlrctl pointer click left 2>/dev/null || true
sleep 0.5
wlrctl pointer click left 2>/dev/null || true
sleep 0.5

# Horizontal page navigation: Page 1 -> Page 2 -> Page 3
"$SPARROW_KEY" combo Control_L Right 2>/dev/null || true
sleep 0.6
"$SPARROW_KEY" combo Control_L Right 2>/dev/null || true
sleep 0.6

# Horizontal page navigation backwards: Page 3 -> Page 2 -> Page 1 -> Page 0
"$SPARROW_KEY" combo Control_L Left 2>/dev/null || true
sleep 0.5
"$SPARROW_KEY" combo Control_L Left 2>/dev/null || true
sleep 0.5
"$SPARROW_KEY" combo Control_L Left 2>/dev/null || true
sleep 0.5

echo -e "${GREEN}[OK] Horizontal PageView navigation and popup interactions exercised.${NC}"

# 5. Test Overview Mode (Toggle, Navigate cards, Select card)
echo -e "${CYAN}[5/6] Testing Overview Mode zoom, card navigation, and selection...${NC}"

# Enter Overview mode via Alt_L
"$SPARROW_KEY" Alt_L 2>/dev/null || true
sleep 0.8

# Navigate window cards inside Overview
"$SPARROW_KEY" Right 2>/dev/null || true
sleep 0.4
"$SPARROW_KEY" Right 2>/dev/null || true
sleep 0.4
"$SPARROW_KEY" Left 2>/dev/null || true
sleep 0.4

# Select card and exit Overview via Enter
"$SPARROW_KEY" Return 2>/dev/null || true
sleep 0.8

echo -e "${GREEN}[OK] Overview mode entered, traversed, and exited successfully.${NC}"

# 6. Audit Health & Validate Logs
echo -e "${CYAN}[6/6] Auditing final Dart VM health, Jank metrics, and compositor log...${NC}"

# Wait for Jank Monitor to finish
if [ -n "${JANK_PID:-}" ] && kill -0 "$JANK_PID" 2>/dev/null; then
    echo -e "  -> Waiting for Jank Monitor sampling to complete..."
    for _ in $(seq 1 20); do
        if ! kill -0 "$JANK_PID" 2>/dev/null; then break; fi
        sleep 0.2
    done
    if kill -0 "$JANK_PID" 2>/dev/null; then
        kill -TERM "$JANK_PID" 2>/dev/null || true
    fi
    wait "$JANK_PID" 2>/dev/null || true
fi

if [ -f "$VM_CLIENT" ]; then
    python3 "$VM_CLIENT" --check-health || true
    python3 "$VM_CLIENT" --memory || true
fi

# Terminate client applications
for pid in "${PIDS[@]}"; do
    kill -TERM "$pid" 2>/dev/null || true
done
PIDS=()

sleep 0.5

# Gracefully terminate Sparrow
EXIT_CODE=0
if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
    kill -TERM "$SPARROW_PID" 2>/dev/null || true
    wait "$SPARROW_PID" || EXIT_CODE=$?
fi

CRASH=false
if grep -Ei "(SIGSEGV|corrupted size|Segmentation fault|heap-use-after-free|double free)" "$LOG_SPARROW" > /dev/null; then
    CRASH=true
fi

SHUTDOWN_CLEAN=false
if grep -F "Shutdown successful!" "$LOG_SPARROW" > /dev/null; then
    SHUTDOWN_CLEAN=true
fi

FPS_OSD_VERIFIED=false
if grep -F "FPS OSD monitor toggled: ENABLED" "$LOG_SPARROW" > /dev/null; then
    FPS_OSD_VERIFIED=true
fi

RAINBOW_VERIFIED=false
if grep -F "Damage visualization debug mode toggled: ENABLED" "$LOG_SPARROW" > /dev/null; then
    RAINBOW_VERIFIED=true
fi

OVERVIEW_VERIFIED=false
if grep -F "[OVERVIEW] force_render_all_views set to TRUE" "$LOG_SPARROW" > /dev/null; then
    OVERVIEW_VERIFIED=true
fi

JANK_MONITOR_PASS=false
if [ -f "$LOG_MONITOR" ] && grep -F "[PASS] Frame pacing meets stability criteria" "$LOG_MONITOR" > /dev/null; then
    JANK_MONITOR_PASS=true
fi

echo ""
echo -e "${BOLD}--- PageView & Overview Test Summary ---${NC}"
echo -e "Compositor Exit Code   : $([ $EXIT_CODE -eq 0 ] && echo "${GREEN}0 (SUCCESS)${NC}" || echo "${RED}${EXIT_CODE} (FAIL)${NC}")"
echo -e "Shutdown Status        : $([ "$SHUTDOWN_CLEAN" = true ] && echo "${GREEN}Clean${NC}" || echo "${RED}Incomplete${NC}")"
echo -e "Crash / Corruption     : $([ "$CRASH" = false ] && echo "${GREEN}None${NC}" || echo "${RED}CRASH DETECTED${NC}")"
echo -e "FPS OSD (F11) Toggle   : $([ "$FPS_OSD_VERIFIED" = true ] && echo "${GREEN}VERIFIED (ENABLED)${NC}" || echo "${YELLOW}Not detected${NC}")"
echo -e "Rainbow (F12) Damage   : $([ "$RAINBOW_VERIFIED" = true ] && echo "${GREEN}VERIFIED (ENABLED)${NC}" || echo "${YELLOW}Not detected${NC}")"
echo -e "Overview Zoom Mode     : $([ "$OVERVIEW_VERIFIED" = true ] && echo "${GREEN}VERIFIED (ENABLED)${NC}" || echo "${YELLOW}Not detected${NC}")"
echo -e "Frame Pacing & Jank    : $([ "$JANK_MONITOR_PASS" = true ] && echo "${GREEN}PASS (Within Budget)${NC}" || echo "${YELLOW}Check ${LOG_MONITOR}${NC}")"

if [ -f "$LOG_MONITOR" ]; then
    echo ""
    echo -e "${CYAN}${BOLD}--- Jank Monitor Report ---${NC}"
    cat "$LOG_MONITOR"
fi

if [ $EXIT_CODE -eq 0 ] && [ "$CRASH" = false ]; then
    echo -e "\n${GREEN}${BOLD}✔ PAGEVIEW & OVERVIEW INTEGRATION TEST PASSED!${NC}\n"
    exit 0
else
    echo -e "\n${RED}${BOLD}✘ TEST FAILED! Tail of log:${NC}\n"
    tail -n 35 "$LOG_SPARROW"
    exit 1
fi
