#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${OUT_DIR}/sparrow"
LOG_SPARROW="${OUT_DIR}/firefox_diag_sparrow.log"
LOG_FIREFOX="${OUT_DIR}/firefox_diag_app.log"

export XDG_RUNTIME_DIR="/tmp/sparrow-runtime-firefox-$$"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"
export mesa_glthread=false
export WLR_BACKENDS="headless"
export WLR_HEADLESS_OUTPUTS="1"

cleanup() {
    pkill -P $$ || true
    rm -rf "$XDG_RUNTIME_DIR"
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
export MOZ_ENABLE_WAYLAND=1
export WAYLAND_DEBUG=1

echo "Launching Firefox with full UI on fresh profile..."
TEMP_PROFILE=$(mktemp -d /tmp/firefox-profile-XXXXXX)
firefox --no-remote --profile "$TEMP_PROFILE" "about:blank" > "$LOG_FIREFOX" 2>&1 &
FIREFOX_PID=$!

sleep 5
echo "Sparrow log (views/popups):"
grep -E "view|popup|Popup" "$LOG_SPARROW" | tail -n 30 || true

kill -TERM "$FIREFOX_PID" 2>/dev/null || true
rm -rf "$TEMP_PROFILE"
kill -TERM "$SPARROW_PID" 2>/dev/null || true
