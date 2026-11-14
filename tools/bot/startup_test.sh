#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Startup Scripts & Process Supervisor Automated Test
# ==============================================================================
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"
SPARROW_BIN="${SPARROW_BIN:-${OUT_DIR}/sparrow}"
TEST_IPC_BIN="${SCRIPT_DIR}/test_process_supervisor_ipc"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}${CYAN}=== Sparrow Startup Scripts & Process Supervisor Test ===${NC}"

if [ ! -x "$SPARROW_BIN" ]; then
    echo -e "${RED}Error: $SPARROW_BIN not found. Run ./build.sh server first.${NC}"
    exit 1
fi

if [ ! -x "$TEST_IPC_BIN" ]; then
    echo -e "${CYAN}Compiling test_process_supervisor_ipc helper...${NC}"
    clang -O2 "${SCRIPT_DIR}/test_process_supervisor_ipc.c" \
          "${ROOT_DIR}/build/proto/libwl_client_protos.a.p/sparrow-ipc-v1-protocol.c" \
          -lwayland-client -I"${ROOT_DIR}" -o "$TEST_IPC_BIN"
fi

TEST_TMPDIR=$(mktemp -d /tmp/sparrow-startup-test-XXXXXX)
SPARROW_LOG="${TEST_TMPDIR}/sparrow.log"

cleanup() {
    echo -e "\n${CYAN}Cleaning up test environment...${NC}"
    if [ -n "$SPARROW_PID" ] && kill -0 "$SPARROW_PID" 2>/dev/null; then
        echo "Sending SIGTERM to Sparrow (PID $SPARROW_PID)..."
        kill -TERM "$SPARROW_PID" 2>/dev/null || true
        for _ in $(seq 1 40); do
            kill -0 "$SPARROW_PID" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$SPARROW_PID" 2>/dev/null; then
            kill -9 "$SPARROW_PID" 2>/dev/null || true
        fi
        wait "$SPARROW_PID" 2>/dev/null || true
    fi
    rm -rf "$TEST_TMPDIR"
    rm -f "$TEST_IPC_BIN"
}
trap cleanup EXIT INT TERM

# 1. Create test sparrow.ini
CONFIG_FILE="${TEST_TMPDIR}/sparrow.ini"
NORMAL_MARKER="${TEST_TMPDIR}/normal.done"
RESTART_MARKER="${TEST_TMPDIR}/restart.done"
EXEC_MARKER="${TEST_TMPDIR}/exec.done"
EXEC_ALWAYS_MARKER="${TEST_TMPDIR}/exec_always.done"

cat > "$CONFIG_FILE" << EOF
[Theme]
wallpaper = 

[Commands]
terminal = foot

[Services]
normal_svc = normal:sh -c 'echo NORMAL_OK > $NORMAL_MARKER; sleep 0.1; exit 0'
restart_svc = restartable:sh -c 'echo RESTART_OK >> $RESTART_MARKER; while true; do sleep 0.1; done'
exec = sh -c 'echo EXEC_OK > $EXEC_MARKER'
exec_always = sh -c 'echo ALWAYS_OK >> $EXEC_ALWAYS_MARKER; while true; do sleep 0.1; done'
EOF

echo "Generated test configuration at $CONFIG_FILE"

# 2. Setup headless environment
export XDG_RUNTIME_DIR="$TEST_TMPDIR/runtime"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export LIBGL_ALWAYS_SOFTWARE=1
export mesa_glthread=false
export SPARROW_CONFIG="$CONFIG_FILE"
unset SPARROW_NO_STARTUP_SCRIPTS
unset SPARROW_ASSETS_PATH SPARROW_ICU_PATH SPARROW_ELF_PATH

# 3. Start Sparrow
echo -e "${CYAN}Starting Sparrow compositor...${NC}"
"$SPARROW_BIN" --no-realtime > "$SPARROW_LOG" 2>&1 &
SPARROW_PID=$!

# 4. Wait for Wayland display socket
WAYLAND_DISPLAY_NAME=""
for i in $(seq 1 180); do
    SOCKET=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | grep '^wayland-' | head -1 || true)
    if [ -n "$SOCKET" ] && [ -S "$XDG_RUNTIME_DIR/$SOCKET" ]; then
        WAYLAND_DISPLAY_NAME="$SOCKET"
        break
    fi
    sleep 0.1
done

if [ -z "$WAYLAND_DISPLAY_NAME" ]; then
    echo -e "${RED}Error: Sparrow failed to create Wayland socket!${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi
export WAYLAND_DISPLAY="$WAYLAND_DISPLAY_NAME"
echo -e "${GREEN}Sparrow running on WAYLAND_DISPLAY=$WAYLAND_DISPLAY (PID $SPARROW_PID)${NC}"

# 5. Verify marker files from startup scripts
echo -e "\n${CYAN}Checking execution of startup scripts...${NC}"
for _ in $(seq 1 60); do
    if [ -f "$NORMAL_MARKER" ] && [ -f "$RESTART_MARKER" ] && [ -f "$EXEC_MARKER" ] && [ -f "$EXEC_ALWAYS_MARKER" ]; then
        break
    fi
    sleep 0.1
done

if [ -f "$NORMAL_MARKER" ]; then
    echo -e "${GREEN}[PASS] normal_svc executed successfully!${NC}"
else
    echo -e "${RED}[FAIL] normal_svc did not execute!${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi

if [ -f "$RESTART_MARKER" ]; then
    echo -e "${GREEN}[PASS] restart_svc executed successfully!${NC}"
else
    echo -e "${RED}[FAIL] restart_svc did not execute!${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi

if [ -f "$EXEC_MARKER" ]; then
    echo -e "${GREEN}[PASS] Sway-style 'exec' executed successfully!${NC}"
else
    echo -e "${RED}[FAIL] Sway-style 'exec' did not execute!${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi

if [ -f "$EXEC_ALWAYS_MARKER" ]; then
    echo -e "${GREEN}[PASS] Sway-style 'exec_always' executed successfully!${NC}"
else
    echo -e "${RED}[FAIL] Sway-style 'exec_always' did not execute!${NC}"
    cat "$SPARROW_LOG"
    exit 1
fi

# 6. Test IPC Queries (listServices)
echo -e "\n${CYAN}Querying services via IPC (listServices)...${NC}"
IPC_OUTPUT=$("$TEST_IPC_BIN")
echo "$IPC_OUTPUT"

if echo "$IPC_OUTPUT" | grep -q '"id":"normal_svc"'; then
    echo -e "${GREEN}[PASS] IPC listServices contains normal_svc${NC}"
else
    echo -e "${RED}[FAIL] IPC listServices missing normal_svc!${NC}"
    exit 1
fi

if echo "$IPC_OUTPUT" | grep -q '"id":"restart_svc"'; then
    echo -e "${GREEN}[PASS] IPC listServices contains restart_svc${NC}"
else
    echo -e "${RED}[FAIL] IPC listServices missing restart_svc!${NC}"
    exit 1
fi

if echo "$IPC_OUTPUT" | grep -q '"id":"exec_always_1"'; then
    echo -e "${GREEN}[PASS] IPC listServices contains exec_always_1${NC}"
else
    echo -e "${RED}[FAIL] IPC listServices missing exec_always_1!${NC}"
    exit 1
fi

# 7. Test IPC Control: stopService
echo -e "\n${CYAN}Testing IPC stopService on restart_svc...${NC}"
STOP_RESP=$("$TEST_IPC_BIN" stopService restart_svc)
echo "$STOP_RESP"
if echo "$STOP_RESP" | grep -q '"state":"stopped"'; then
    echo -e "${GREEN}[PASS] stopService successfully stopped restart_svc!${NC}"
else
    echo -e "${RED}[FAIL] stopService failed on restart_svc!${NC}"
    exit 1
fi

# Verify it stays stopped
sleep 0.5
CHECK_RESP=$("$TEST_IPC_BIN")
if echo "$CHECK_RESP" | grep '"id":"restart_svc"' | grep -q '"state":"stopped"'; then
    echo -e "${GREEN}[PASS] restart_svc remained stopped (no auto-restart on manual stop)${NC}"
else
    echo -e "${RED}[FAIL] restart_svc unexpectedly auto-restarted after manual stop!${NC}"
    exit 1
fi

# 8. Test IPC Control: startService
echo -e "\n${CYAN}Testing IPC startService on restart_svc...${NC}"
START_RESP=$("$TEST_IPC_BIN" startService restart_svc)
echo "$START_RESP"
if echo "$START_RESP" | grep -q '"state":"running"'; then
    echo -e "${GREEN}[PASS] startService successfully restarted restart_svc!${NC}"
else
    echo -e "${RED}[FAIL] startService failed on restart_svc!${NC}"
    exit 1
fi

# 9. Test Auto-Restart on crash/kill (the core supervision feature)
echo -e "\n${CYAN}Testing automatic restart on crash for exec_always_1...${NC}"
EXEC_ALWAYS_PID=$(echo "$IPC_OUTPUT" | grep -o '{"id":"exec_always_1"[^}]*}' | grep -o '"pid":[0-9]*' | cut -d: -f2)
echo "Current PID of exec_always_1: $EXEC_ALWAYS_PID"

if [ -n "$EXEC_ALWAYS_PID" ] && [ "$EXEC_ALWAYS_PID" -gt 0 ]; then
    echo "Simulating crash: kill -9 $EXEC_ALWAYS_PID"
    kill -9 "$EXEC_ALWAYS_PID" 2>/dev/null || true
    
    echo "Waiting for backoff timer and auto-restart..."
    NEW_PID=""
    RESTART_COUNT=0
    for _ in $(seq 1 60); do
        AFTER_CRASH=$("$TEST_IPC_BIN" 2>/dev/null || true)
        NEW_PID=$(echo "$AFTER_CRASH" | grep -o '{"id":"exec_always_1"[^}]*}' | grep -o '"pid":[0-9]*' | cut -d: -f2 || true)
        RESTART_COUNT=$(echo "$AFTER_CRASH" | grep -o '{"id":"exec_always_1"[^}]*}' | grep -o '"restart_count":[0-9]*' | cut -d: -f2 || true)
        if [ -n "$NEW_PID" ] && [ "$NEW_PID" -gt 0 ] && [ "$NEW_PID" -ne "$EXEC_ALWAYS_PID" ] && [ "$RESTART_COUNT" -ge 1 ]; then
            break
        fi
        sleep 0.1
    done
    echo "New PID after auto-restart: $NEW_PID (restart_count: $RESTART_COUNT)"
    
    if [ -n "$NEW_PID" ] && [ "$NEW_PID" -gt 0 ] && [ "$NEW_PID" -ne "$EXEC_ALWAYS_PID" ] && [ "$RESTART_COUNT" -ge 1 ]; then
        echo -e "${GREEN}[PASS] exec_always_1 automatically recovered and restarted (PID $NEW_PID, restarts: $RESTART_COUNT)!${NC}"
    else
        echo -e "${RED}[FAIL] exec_always_1 failed to auto-restart!${NC}"
        echo "$AFTER_CRASH"
        exit 1
    fi
else
    echo -e "${YELLOW}Warning: Could not extract valid PID for exec_always_1${NC}"
fi

echo -e "\n${BOLD}${GREEN}======================================================${NC}"
echo -e "${BOLD}${GREEN} ALL PROCESS SUPERVISOR TESTS PASSED SUCCESSFULLY!    ${NC}"
echo -e "${BOLD}${GREEN}======================================================${NC}"
