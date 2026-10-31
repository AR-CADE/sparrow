#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
LOG_SPARROW="${OUT_DIR}/idle_mpv_sparrow.log"
LOG_SWAYIDLE="${OUT_DIR}/idle_mpv_swayidle.log"

VIDEO="/tmp/mpv_test/test_5s.mp4"

cleanup() {
    kill -TERM "$SWAYIDLE_PID" 2>/dev/null || true
    kill -TERM "$MPV_PID" 2>/dev/null || true
    kill -TERM "$SPARROW_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "[1/4] Launching Sparrow..."
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
    echo "Sparrow failed to start"
    exit 1
fi
echo "Sparrow ready on $SPARROW_DISPLAY"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

echo "[2/4] Starting swayidle with 2s timeout..."
swayidle -d timeout 2 "echo [SWAYIDLE_TIMEOUT_TRIGGERED]" resume "echo [SWAYIDLE_RESUME_TRIGGERED]" > "$LOG_SWAYIDLE" 2>&1 &
SWAYIDLE_PID=$!

sleep 3.0
echo "Swayidle log before mpv:"
cat "$LOG_SWAYIDLE"

echo "[3/4] Launching mpv (without --fs, letting delayed_fullscreen.lua transition to fullscreen)..."
flatpak run io.mpv.Mpv "$VIDEO" || true
sleep 1.0

echo "[4/4] mpv has closed. Waiting 6 seconds to see if swayidle triggers..."
sleep 6.0

echo "=== Final Swayidle Log ==="
cat "$LOG_SWAYIDLE"

echo "=== Sparrow Idle Inhibitor Log ==="
grep -E "IDLE-INHIBIT|DIRECT-SCANOUT|idle" "$LOG_SPARROW" || true
