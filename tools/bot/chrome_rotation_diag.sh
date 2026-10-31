#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"
mkdir -p "$OUT_DIR"

SPARROW_BIN="${OUT_DIR}/sparrow"
LOG_SPARROW="${OUT_DIR}/chrome_diag_sparrow.log"
LOG_CHROME="${OUT_DIR}/chrome_diag_app.log"

export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-chrome-$$"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"
export mesa_glthread=false
export WLR_BACKENDS="headless"
export WLR_HEADLESS_OUTPUTS="1"

cleanup() {
    pkill -P $$ || true
    rm -rf "$XDG_RUNTIME_DIR"
    rm -rf "/tmp/chrome-test-profile-$$"
}
trap cleanup EXIT INT TERM

echo "Launching Sparrow headless..."
"$SPARROW_BIN" --no-realtime > "$LOG_SPARROW" 2>&1 &
SPARROW_PID=$!

SPARROW_DISPLAY=""
for _ in $(seq 1 60); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$LOG_SPARROW" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$LOG_SPARROW" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$XDG_RUNTIME_DIR/$SPARROW_DISPLAY" ]; then
            break
        fi
    fi
    sleep 0.1
done

if [ -z "$SPARROW_DISPLAY" ]; then
    echo "Sparrow failed to start"
    cat "$LOG_SPARROW"
    exit 1
fi
echo "Compositor ready on $SPARROW_DISPLAY"
export WAYLAND_DISPLAY="$SPARROW_DISPLAY"

echo "Launching Chromium..."
mkdir -p "/tmp/chrome-test-profile-$$"
chromium \
    --ozone-platform=wayland \
    --enable-features=UseOzonePlatform \
    --no-sandbox \
    --user-data-dir="/tmp/chrome-test-profile-$$" \
    --no-first-run \
    --disable-fre \
    --disable-gpu-early-init \
    --start-maximized \
    "about:blank" > "$LOG_CHROME" 2>&1 &
CHROME_PID=$!

echo "Waiting for Chromium to map..."
for _ in $(seq 1 100); do
    if grep -E "XDG TOPLEVEL MAP: view=" "$LOG_SPARROW" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
sleep 2

OUTPUT_NAME=$(wlr-randr 2>/dev/null | grep -oP '^\S+' | head -n 1 || true)
if [ -z "$OUTPUT_NAME" ]; then
    OUTPUT_NAME="HEADLESS-1"
fi
echo "Target output: $OUTPUT_NAME"

trigger_frame() {
    local wlrctl_bin="${WLRCTL_BIN:-${OUT_DIR}/wlrctl}"
    if [ ! -x "$wlrctl_bin" ] && [ -x "${ROOT_DIR}/build/subprojects/wlrctl/wlrctl" ]; then
        wlrctl_bin="${ROOT_DIR}/build/subprojects/wlrctl/wlrctl"
    elif [ ! -x "$wlrctl_bin" ] && command -v wlrctl &>/dev/null; then
        wlrctl_bin="$(command -v wlrctl)"
    fi
    if [ -x "$wlrctl_bin" ]; then
        "$wlrctl_bin" pointer move 1 1 2>/dev/null || true
        "$wlrctl_bin" pointer move -1 -1 2>/dev/null || true
    fi
}

take_screenshot() {
    local target="$1"
    trigger_frame
    sleep 0.2
    timeout 3 grim "$target" 2>/dev/null || true
}

echo "Taking initial landscape screenshot..."
take_screenshot "${OUT_DIR}/chrome_landscape1.png"
sleep 1

echo "Rotating to portrait (90 degrees)..."
wlr-randr --output "$OUTPUT_NAME" --transform 90 || true
sleep 2

echo "Taking portrait screenshot..."
take_screenshot "${OUT_DIR}/chrome_portrait.png"
sleep 1

echo "Rotating back to landscape (normal)..."
wlr-randr --output "$OUTPUT_NAME" --transform normal || true
sleep 2

echo "Taking return landscape screenshot..."
take_screenshot "${OUT_DIR}/chrome_landscape2.png"
sleep 1

echo "=== Sparrow Surface Logs ==="
grep -E "Surface map|Surface geometry|XDG TOPLEVEL MAP|xdg_toplevel_commit" "$LOG_SPARROW" | tail -n 30 || true

echo "Done."
