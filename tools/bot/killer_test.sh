#!/bin/bash
# ==============================================================================
# Sparrow - Compositor Killer ("I Will Kill You") Torture & Resilience Test
# ==============================================================================
# Tests compositor resistance against malicious GPU shader workloads
# and implicit sync stalling using Scott Anderson's 'compositor-killer'.
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
KILLER_BIN="${OUT_DIR}/compositor-killer"

SPARROW_LOG="${OUT_DIR}/sparrow_killer.log"
KILLER_LOG="${OUT_DIR}/compositor_killer.log"

DURATION_SEC=60
LEVEL="medium"
HEADLESS=false
CUSTOM_ARGS=""
SHOW_LOGS=false

for arg in "$@"; do
    case "$arg" in
        --level=*|-L=*)
            LEVEL="${arg#*=}"
            ;;
        --duration=*|-d=*)
            DURATION_SEC="${arg#*=}"
            ;;
        --headless|-H)
            HEADLESS=true
            ;;
        --show-logs|-l)
            SHOW_LOGS=true
            ;;
        --args=*)
            CUSTOM_ARGS="${arg#*=}"
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Modes / Difficulty Levels:"
            echo "  --level=easy     Mild Mandelbrot workload (-i 1000 -a 1)"
            echo "  --level=medium   Heavy GPU shader workload (-i 5000 -a 2) [default]"
            echo "  --level=hard     Stalling GPU load + unsynchronized (-i 15000 -a 2 -u)"
            echo "  --level=deadly   Maximum GPU stress (-i 30000 -a 2 -u)"
            echo ""
            echo "Options:"
            echo "  --duration=N     Test execution duration in seconds (default: 60s)"
            echo "  --headless, -H   Run compositor in headless mode"
            echo "  --show-logs, -l  Display logs after execution"
            echo "  --args=\"...\"     Custom arguments passed to compositor-killer"
            echo "  --help, -h       Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    echo "Run ./build.sh first."
    exit 1
fi

if [ ! -f "$KILLER_BIN" ]; then
    echo -e "${RED}[ERROR] compositor-killer binary not found at ${KILLER_BIN}.${NC}"
    echo "Run ./build.sh server first."
    exit 1
fi

case "$LEVEL" in
    easy)
        CK_ARGS="-i 1000 -a 1"
        ;;
    medium)
        CK_ARGS="-i 5000 -a 2"
        ;;
    hard)
        CK_ARGS="-i 15000 -a 2 -u"
        ;;
    deadly)
        CK_ARGS="-i 30000 -a 2 -u"
        ;;
    *)
        CK_ARGS="$CUSTOM_ARGS"
        ;;
esac

if [ -n "$CUSTOM_ARGS" ]; then
    CK_ARGS="$CUSTOM_ARGS"
fi

echo -e "${MAGENTA}${BOLD}=== Sparrow 'I Will Kill You' Stress & Resilience Test ===${NC}"
echo -e "Target Compositor : ${BOLD}${SPARROW_BIN}${NC}"
echo -e "Target Killer     : ${BOLD}${KILLER_BIN}${NC}"
echo -e "Difficulty Level  : ${BOLD}${LEVEL}${NC} (${CK_ARGS})"
echo -e "Target Duration   : ${BOLD}${DURATION_SEC}s (>= 1 min torture)${NC}"
echo -e "Headless Mode     : ${BOLD}${HEADLESS}${NC}"
echo -e "Compositor Log    : ${BOLD}${SPARROW_LOG}${NC}"
echo -e "Killer Log        : ${BOLD}${KILLER_LOG}${NC}"

# Runtime environment
if [ -z "$XDG_RUNTIME_DIR" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-$$"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
fi

export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"

if [ -f "${ROOT_DIR}/tsan_suppressions.txt" ]; then
    if [ -z "$TSAN_OPTIONS" ]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:report_signal_unsafe=0:second_deadlock_stack=1"
    elif [[ "$TSAN_OPTIONS" != *"suppressions="* ]]; then
        export TSAN_OPTIONS="suppressions=${ROOT_DIR}/tsan_suppressions.txt:report_signal_unsafe=0:second_deadlock_stack=1:${TSAN_OPTIONS}"
    fi
fi

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS=headless
    export WLR_HEADLESS_OUTPUTS=1
    export LIBGL_ALWAYS_SOFTWARE=1
    export SPARROW_NO_REALTIME=1
fi

rm -f "$SPARROW_LOG" "$KILLER_LOG"

SPARROW_PID=""
KILLER_PID=""

cleanup() {
    if [ -n "$KILLER_PID" ] && kill -0 "$KILLER_PID" 2>/dev/null; then
        kill -TERM "$KILLER_PID" 2>/dev/null || true
        wait "$KILLER_PID" 2>/dev/null || true
    fi
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        kill -KILL "$SPARROW_PID" 2>/dev/null || true
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    if [[ "$XDG_RUNTIME_DIR" == /tmp/sparrow-runtime-* ]]; then
        rm -rf "$XDG_RUNTIME_DIR"
    fi
}
trap cleanup EXIT INT TERM

# 1. Start Sparrow Compositor
echo -e "\n${BLUE}[1/4] Starting Sparrow compositor...${NC}"
"$SPARROW_BIN" --no-realtime > "$SPARROW_LOG" 2>&1 &
SPARROW_PID=$!

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
    echo -e "${RED}[FAIL] Sparrow failed to initialize Wayland socket.${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi

echo -e "${GREEN}[OK] Sparrow running on ${CYAN}${SPARROW_DISPLAY}${GREEN} (PID: ${SPARROW_PID})${NC}"

# 2. Launch compositor-killer
echo -e "\n${BLUE}[2/4] Unleashing compositor-killer...${NC}"
WAYLAND_DISPLAY="$SPARROW_DISPLAY" "$KILLER_BIN" $CK_ARGS > "$KILLER_LOG" 2>&1 &
KILLER_PID=$!

sleep 0.5
if ! kill -0 "$KILLER_PID" 2>/dev/null; then
    echo -e "${RED}[ERROR] compositor-killer failed to start!${NC}"
    cat "$KILLER_LOG"
    exit 1
fi
echo -e "${GREEN}[OK] compositor-killer attached to Sparrow (PID: ${KILLER_PID})${NC}"

# 3. Monitor Survival under attack for DURATION_SEC seconds
echo -e "\n${BLUE}[3/4] Surviving malicious workload (${DURATION_SEC}s)...${NC}"
START_TIME=$(date +%s)
SPARROW_DIED=false
ELAPSED=0

while [ "$ELAPSED" -lt "$DURATION_SEC" ]; do
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        SPARROW_DIED=true
        break
    fi
    sleep 2
    ELAPSED=$(($(date +%s) - START_TIME))
    # Display heartbeat status every 10s
    if [ $((ELAPSED % 10)) -lt 2 ]; then
        FRAMES=$(grep -c "Frame " "$KILLER_LOG" 2>/dev/null || echo 0)
        echo -e "  [Heartbeat] ${ELAPSED}s / ${DURATION_SEC}s elapsed - Compositor alive (PID ${SPARROW_PID}) - Killer frames rendered: ${FRAMES}"
    fi
done

if [ "$SPARROW_DIED" = true ]; then
    echo -e "\n${RED}${BOLD}💀 SPARROW WAS KILLED DURING ATTACK!${NC}"
    echo -e "${RED}The compositor crashed or was terminated under GPU workload!${NC}"
    echo -e "\n--- Compositor Log (Tail) ---"
    tail -n 40 "$SPARROW_LOG"
    exit 1
fi

echo -e "\n${GREEN}[OK] Sparrow survived the ${DURATION_SEC}s attack!${NC}"

# 4. Graceful Logout Test (1 minute mark)
echo -e "\n${BLUE}[4/4] Testing graceful logout (SIGTERM) while under heavy workload...${NC}"
LOGOUT_SUCCESS=false

# Send SIGTERM to Sparrow to trigger clean logout
kill -TERM "$SPARROW_PID" 2>/dev/null || true

# Wait up to 10 seconds for graceful shutdown
for i in $(seq 1 20); do
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        LOGOUT_SUCCESS=true
        break
    fi
    sleep 0.5
done

if [ "$LOGOUT_SUCCESS" = false ]; then
    echo -e "${RED}${BOLD}[FAIL] SPARROW HUNG / DEADLOCKED ON LOGOUT!${NC}"
    echo -e "${RED}Compositor did not shut down within 10s (likely stuck in GPU/fence lock). Killing with SIGKILL.${NC}"
    kill -KILL "$SPARROW_PID" 2>/dev/null || true
    echo -e "\n--- Compositor Log (Tail) ---"
    tail -n 40 "$SPARROW_LOG"
    exit 1
fi

echo -e "${GREEN}[OK] Sparrow cleanly shut down and logged out without deadlocks!${NC}"

# Stop the killer now that compositor is logged out
if [ -n "$KILLER_PID" ] && kill -0 "$KILLER_PID" 2>/dev/null; then
    kill -TERM "$KILLER_PID" 2>/dev/null || true
    wait "$KILLER_PID" 2>/dev/null || true
fi

echo ""
echo -e "${BOLD}======================================================${NC}"
echo -e "${BOLD}              KILLER TEST SUMMARY RESULTS             ${NC}"
echo -e "${BOLD}======================================================${NC}"
echo -e "${GREEN}${BOLD}🛡️  SPARROW SURVIVED AND DODGED ALL BULLETS!${NC}"
echo -e "${GREEN}1 minute torture survived + clean graceful logout verified.${NC}"

if [ -f "$KILLER_LOG" ]; then
    FRAME_COUNT=$(grep -c "Frame " "$KILLER_LOG" 2>/dev/null || echo 0)
    echo -e "Killer frames rendered : ${BOLD}${FRAME_COUNT}${NC}"
    if [ "$FRAME_COUNT" -gt 0 ]; then
        echo -e "Latest Frame Timings   :"
        tail -n 5 "$KILLER_LOG"
    fi
fi
echo -e "${BOLD}======================================================${NC}"

if [ "$SHOW_LOGS" = true ]; then
    echo -e "\n${BOLD}--- Full Killer Log ---${NC}"
    cat "$KILLER_LOG"
fi

exit 0
