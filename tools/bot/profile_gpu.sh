#!/usr/bin/env bash
# ==============================================================================
# Sparrow - GPU & Performance Profiling Harness (Perfetto + RenderDoc)
# ==============================================================================
# Automates in-process GPU and compositor tracing using Perfetto Protobuf traces
# (.pftrace) and optional RenderDoc frame capture without requiring sudo/root.
#
# Usage:
#   ./tools/bot/profile_gpu.sh [OPTIONS]
#
# Examples:
#   ./tools/bot/profile_gpu.sh --duration=5
#   ./tools/bot/profile_gpu.sh --duration=10 --app=out/demo_app
#   ./tools/bot/profile_gpu.sh --output=out/my_trace.pftrace
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
RUNNER_BIN="${OUT_DIR}/sparrow-app-runner"
DEFAULT_APP="${OUT_DIR}/demo_app"
DEFAULT_TRACE="${OUT_DIR}/sparrow_profile.pftrace"

SPARROW_LOG="${OUT_DIR}/sparrow_profile.log"
RUNNER_LOG="${OUT_DIR}/runner_profile.log"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m'

DURATION_SEC=5
APP_BUNDLE="$DEFAULT_APP"
OUTPUT_TRACE="$DEFAULT_TRACE"
HEADLESS=false
VERBOSE=false
RENDERDOC_CAPTURE=0

for arg in "$@"; do
    case "$arg" in
        --duration=*|-d=*)
            DURATION_SEC="${arg#*=}"
            ;;
        --app=*|-a=*)
            APP_BUNDLE="${arg#*=}"
            ;;
        --output=*|-o=*)
            OUTPUT_TRACE="${arg#*=}"
            ;;
        --renderdoc=*|-r=*)
            RENDERDOC_CAPTURE="${arg#*=}"
            ;;
        --renderdoc|-r)
            RENDERDOC_CAPTURE=15
            ;;
        --headless|-H)
            HEADLESS=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --duration=N, -d=N       Profiling duration in seconds (default: 5s)"
            echo "  --app=PATH, -a=PATH      Flutter app bundle to profile (default: out/demo_app)"
            echo "  --output=PATH, -o=PATH   Perfetto trace output file (default: out/sparrow_profile.pftrace)"
            echo "  --renderdoc[=FRAME], -r  Trigger RenderDoc capture at frame N (default: 15)"
            echo "  --headless, -H           Run compositor headless (WLR_BACKENDS=headless)"
            echo "  --verbose, -v            Stream logs in real-time"
            echo "  --help, -h               Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

echo -e "${CYAN}${BOLD}=== Sparrow GPU & Performance Profiling Harness ===${NC}"
echo -e "Target Compositor : ${BOLD}${SPARROW_BIN}${NC}"
echo -e "Target Runner     : ${BOLD}${RUNNER_BIN}${NC}"
echo -e "Target App Bundle : ${BOLD}${APP_BUNDLE}${NC}"
echo -e "Perfetto Trace    : ${BOLD}${OUTPUT_TRACE}${NC}"
echo -e "Duration          : ${BOLD}${DURATION_SEC}s${NC}"
if [ "$RENDERDOC_CAPTURE" -gt 0 ]; then
    echo -e "RenderDoc Capture : ${BOLD}Frame ${RENDERDOC_CAPTURE}${NC}"
fi

# 1. Prerequisite verification
if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    echo "Run ./build.sh trace first."
    exit 1
fi

# Verify trace symbols exist
if ! grep -i -a -q "perfetto" "$SPARROW_BIN"; then
    echo -e "${YELLOW}[WARNING] Sparrow was not compiled with trace enabled.${NC}"
    echo -e "${YELLOW}Rebuilding in trace mode (./build.sh trace server)...${NC}"
    "${ROOT_DIR}/build.sh" trace server
fi

# 2. Cleanup old artifacts
rm -f "$OUTPUT_TRACE" "$SPARROW_LOG" "$RUNNER_LOG"
mkdir -p "$(dirname "$OUTPUT_TRACE")"

# 3. Setup isolated test environment
RUNTIME_PATH="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export RENDERDOC_CAPFILE="${OUT_DIR}/sparrow_renderdoc"
export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"

if [ "$HEADLESS" = true ]; then
    export WLR_BACKENDS="headless"
    export WLR_HEADLESS_OUTPUTS=1
    export LIBGL_ALWAYS_SOFTWARE=1
    export SPARROW_NO_REALTIME=1
fi

SPARROW_PID=""
RUNNER_PID=""
SPARROW_DISPLAY=""

cleanup() {
    echo ""
    echo -e "${YELLOW}[PROFILER] Stopping profiling session and flushing trace data...${NC}"
    
    if [ -n "$RUNNER_PID" ] && kill -0 "$RUNNER_PID" 2>/dev/null; then
        kill -TERM "$RUNNER_PID" 2>/dev/null || true
        wait "$RUNNER_PID" 2>/dev/null || true
    fi

    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        # SIGTERM triggers clean Sparrow shutdown and flushes Perfetto trace file
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        
        # Give compositor up to 5 seconds to flush trace and terminate
        local wait_count=0
        while kill -0 "$SPARROW_PID" 2>/dev/null && [ $wait_count -lt 50 ]; do
            sleep 0.1
            wait_count=$((wait_count + 1))
        done
        
        if kill -0 "$SPARROW_PID" 2>/dev/null; then
            kill -KILL "$SPARROW_PID" 2>/dev/null || true
        fi
    fi
}

trap cleanup EXIT INT TERM

# 4. Launch Sparrow compositor with in-process tracing
SPARROW_ARGS=(
    "--no-realtime"
    "--trace-perfetto=${OUTPUT_TRACE}"
)

if [ "$RENDERDOC_CAPTURE" -gt 0 ]; then
    SPARROW_ARGS+=("--renderdoc-capture=${RENDERDOC_CAPTURE}")
fi

echo -e "${BLUE}[1/4] Launching Sparrow compositor with in-process Perfetto tracing...${NC}"
if [ "$VERBOSE" = true ]; then
    "$SPARROW_BIN" "${SPARROW_ARGS[@]}" 2>&1 | tee "$SPARROW_LOG" &
else
    "$SPARROW_BIN" "${SPARROW_ARGS[@]}" > "$SPARROW_LOG" 2>&1 &
fi
SPARROW_PID=$!

# Wait for Wayland display socket to become ready
for _ in $(seq 1 80); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$SPARROW_LOG" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$SPARROW_LOG" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$RUNTIME_PATH/$SPARROW_DISPLAY" ]; then
            break
        fi
    fi
    if ! kill -0 "$SPARROW_PID" 2>/dev/null; then
        echo -e "${RED}[ERROR] Sparrow compositor crashed during startup!${NC}"
        cat "$SPARROW_LOG"
        exit 1
    fi
    sleep 0.1
done

if [ -z "$SPARROW_DISPLAY" ] || [ ! -S "$RUNTIME_PATH/$SPARROW_DISPLAY" ]; then
    echo -e "${RED}[ERROR] Timeout waiting for Sparrow Wayland socket.${NC}"
    if [ -f "$SPARROW_LOG" ]; then
        cat "$SPARROW_LOG"
    fi
    exit 1
fi
echo -e "${GREEN}[OK] Sparrow compositor active on socket: ${CYAN}${SPARROW_DISPLAY}${GREEN} (PID: ${SPARROW_PID})${NC}"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

# 5. Launch Flutter App Runner if available
if [ -f "$RUNNER_BIN" ] && [ -d "$APP_BUNDLE" ]; then
    echo -e "${BLUE}[2/4] Launching Flutter App Runner workload (${APP_BUNDLE})...${NC}"
    if [ "$VERBOSE" = true ]; then
        "$RUNNER_BIN" --bundle="$APP_BUNDLE" 2>&1 | tee "$RUNNER_LOG" &
    else
        "$RUNNER_BIN" --bundle="$APP_BUNDLE" > "$RUNNER_LOG" 2>&1 &
    fi
    RUNNER_PID=$!
else
    echo -e "${YELLOW}[INFO] App runner or demo app not found; profiling compositor workload directly.${NC}"
fi

# 6. Profiling duration with visual progress
echo -e "${BLUE}[3/4] Recording GPU and system trace for ${DURATION_SEC}s...${NC}"
for ((i=1; i<=DURATION_SEC; i++)); do
    sleep 1
    echo -ne "  Profiling progress: [${i}/${DURATION_SEC}s]\r"
done
echo ""

# 7. Stop and flush
echo -e "${BLUE}[4/4] Finalizing profiling session...${NC}"
cleanup
trap - EXIT INT TERM

# 8. Verification and analysis of trace artifact
echo ""
echo -e "${CYAN}${BOLD}=== Profiling Results ===${NC}"

if [ ! -f "$OUTPUT_TRACE" ]; then
    echo -e "${RED}[FAIL] Trace artifact was not generated: ${OUTPUT_TRACE}${NC}"
    if [ -f "$SPARROW_LOG" ]; then
        echo -e "${YELLOW}Last 20 lines of compositor log:${NC}"
        tail -n 20 "$SPARROW_LOG"
    fi
    exit 1
fi

TRACE_SIZE=$(stat -c %s "$OUTPUT_TRACE" 2>/dev/null || stat -f %z "$OUTPUT_TRACE" 2>/dev/null || echo "0")

if [ "$TRACE_SIZE" -le 0 ]; then
    echo -e "${RED}[FAIL] Trace file ${OUTPUT_TRACE} is empty (0 bytes).${NC}"
    exit 1
fi

# Format size for readability
if [ "$TRACE_SIZE" -ge 1048576 ]; then
    HUMAN_SIZE="$(awk "BEGIN {printf \"%.2f MB\", $TRACE_SIZE / 1048576}")"
elif [ "$TRACE_SIZE" -ge 1024 ]; then
    HUMAN_SIZE="$(awk "BEGIN {printf \"%.2f KB\", $TRACE_SIZE / 1024}")"
else
    HUMAN_SIZE="${TRACE_SIZE} bytes"
fi

echo -e "Trace Artifact : ${GREEN}${BOLD}${OUTPUT_TRACE}${NC}"
echo -e "Artifact Size  : ${GREEN}${BOLD}${HUMAN_SIZE}${NC} (${TRACE_SIZE} bytes)"
echo -e "Status         : ${GREEN}${BOLD}SUCCESS${NC}"
echo ""
echo -e "${MAGENTA}${BOLD}👉 How to visualize:${NC}"
echo -e "  1. Open ${CYAN}https://ui.perfetto.dev${NC} in your browser"
echo -e "  2. Drag and drop ${BOLD}${OUTPUT_TRACE}${NC} into the UI"
echo -e "  3. Inspect GPU frame times, compositor events, Flutter present layers, and IPC messages!"
echo ""

# Check for RenderDoc captures if requested
RENDERDOC_FILES=(out/sparrow_renderdoc*.rdc)
if [ -e "${RENDERDOC_FILES[0]}" ]; then
    echo -e "RenderDoc Capture : ${GREEN}${BOLD}${RENDERDOC_FILES[*]}${NC}"
    echo -e "  Inspect with: ${CYAN}qrenderdoc ${RENDERDOC_FILES[0]}${NC}"
    echo ""
fi

exit 0
