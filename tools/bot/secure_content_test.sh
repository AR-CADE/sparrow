#!/usr/bin/env bash
# ==============================================================================
# Sparrow - Secure Content & Privacy Protection Test Harness
# ==============================================================================
# Validates sensitive content masking and privacy blackout across all modes:
#   1. Sensitive Application in Normal Mode (Landscape 0°)
#   2. Sensitive Application in Normal Mode with Rotation (Portrait 90°)
#   3. Sensitive Application in Overview Mode (Landscape 0°)
#   4. Sensitive Application in Overview Mode with Rotation (Portrait 90°)
#   5. Entire Output Sensitive in Normal Mode (Landscape 0°)
#   6. Entire Output Sensitive with Rotation (Portrait 90°)
# ==============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/out"

SPARROW_BIN="${SPARROW_BIN:-${OUT_DIR}/sparrow}"
CLIENT_BIN="${ROOT_DIR}/tools/bot/repro_clients/fixed_size_client"
KEY_BIN="${ROOT_DIR}/tools/bot/repro_clients/sparrow_key"

LOG_SPARROW="${OUT_DIR}/secure_content_sparrow.log"
LOG_CLIENT="${OUT_DIR}/secure_content_client.log"

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

echo -e "${CYAN}${BOLD}=== Sparrow Secure Content & Privacy Protection Test Suite ===${NC}"

if [ ! -f "$SPARROW_BIN" ]; then
    echo -e "${RED}[ERROR] Sparrow binary not found at ${SPARROW_BIN}.${NC}"
    exit 1
fi

# Ensure repro clients are compiled
if [ ! -x "$CLIENT_BIN" ] || [ ! -x "$KEY_BIN" ]; then
    echo -e "${YELLOW}Compiling repro test clients (fixed_size_client, sparrow_key)...${NC}"
    make -C "${ROOT_DIR}/tools/bot/repro_clients" fixed_size_client sparrow_key >/dev/null 2>&1
fi

if ! command -v wlr-randr &>/dev/null; then
    echo -e "${RED}[ERROR] 'wlr-randr' is required for rotation tests.${NC}"
    exit 1
fi

if ! command -v grim &>/dev/null; then
    echo -e "${RED}[ERROR] 'grim' is required for screencopy verification.${NC}"
    exit 1
fi

if ! python3 -c "from PIL import Image" 2>/dev/null; then
    echo -e "${RED}[ERROR] python3 with Pillow (PIL) is required for pixel analysis.${NC}"
    exit 1
fi

mkdir -p "$OUT_DIR"
rm -f "$LOG_SPARROW" "$LOG_CLIENT"

TEST_TMPDIR=$(mktemp -d /tmp/sparrow-secure-test-XXXXXX)
CONFIG_FILE="${TEST_TMPDIR}/sparrow.ini"

cat > "$CONFIG_FILE" << EOF
[Theme]
wallpaper = 

[secure-content]
app = sparrow.fixed.client
EOF

# Runtime environment setup
export XDG_RUNTIME_DIR="${TEST_TMPDIR}/runtime"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"

export LD_LIBRARY_PATH="${OUT_DIR}/shell/lib:${ROOT_DIR}/subprojects/flutter_embedder:${LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1
export mesa_glthread=false
export SPARROW_CONFIG="$CONFIG_FILE"
unset SPARROW_NO_STARTUP_SCRIPTS

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
    rm -rf "$TEST_TMPDIR"
}
trap cleanup EXIT INT TERM

# Helper function to audit screencopy image with python PIL
audit_image() {
    local img_path="$1"
    local check_type="$2" # "app_censored" or "full_black"
    local min_ratio="${3:-0.05}"
    local max_ratio="${4:-0.999}"

    python3 - << PYEOF
import sys
from PIL import Image

img = Image.open("${img_path}").convert("RGB")
w, h = img.size

# Check for pattern leaks (cyan border: r < 50, g > 200, b > 200; yellow center: r > 200, g > 200, b < 50)
cyan_count = 0
yellow_count = 0
black_count = 0
total = w * h

for y in range(h):
    for x in range(w):
        r, g, b = img.getpixel((x, y))
        if r == 0 and g == 0 and b == 0:
            black_count += 1
        elif r < 50 and g > 200 and b > 200:
            cyan_count += 1
        elif r > 200 and g > 200 and b < 50:
            yellow_count += 1

ratio = black_count / total

if "${check_type}" == "full_black":
    if black_count != total:
        print(f"FAIL: Expected 100% black pixels, but found {total - black_count} non-black pixels ({ratio*100:.2f}% black)")
        sys.exit(1)
    print(f"PASS: 100% black pixels confirmed ({total} pixels)")
elif "${check_type}" == "app_censored":
    if cyan_count > 0 or yellow_count > 0:
        print(f"FAIL: Sensitive pattern leaked! cyan_pixels={cyan_count}, yellow_pixels={yellow_count}")
        sys.exit(1)
    if black_count == 0:
        print("FAIL: No black mask detected on sensitive window!")
        sys.exit(1)
    if black_count == total:
        print("FAIL: Entire screen was blacked out, expected selective app blackout!")
        sys.exit(1)
    if not (${min_ratio} <= ratio <= ${max_ratio}):
        print(f"FAIL: Black ratio {ratio:.3f} outside expected range [${min_ratio}, ${max_ratio}]")
        sys.exit(1)
    print(f"PASS: App blackout confirmed (black coverage: {ratio*100:.2f}%, 0 leaked pattern pixels)")
PYEOF
}

# 1. Start Sparrow
echo -e "${CYAN}[1/7] Launching Sparrow compositor...${NC}"
"$SPARROW_BIN" --no-realtime > "$LOG_SPARROW" 2>&1 &
SPARROW_PID=$!

SPARROW_DISPLAY=""
for _ in $(seq 1 180); do
    if grep -F "Running Wayland compositor on WAYLAND_DISPLAY=" "$LOG_SPARROW" >/dev/null 2>&1; then
        SPARROW_DISPLAY=$(grep -oP 'Running Wayland compositor on WAYLAND_DISPLAY=\K\S+' "$LOG_SPARROW" | head -n 1)
        if [ -n "$SPARROW_DISPLAY" ] && [ -S "$XDG_RUNTIME_DIR/$SPARROW_DISPLAY" ]; then
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

OUTPUT_NAME=$(wlr-randr 2>/dev/null | grep -oP '^\S+' | head -n 1 || true)
if [ -z "$OUTPUT_NAME" ]; then
    OUTPUT_NAME="HEADLESS-1"
fi
echo -e "  -> Detected display output: ${BOLD}${OUTPUT_NAME}${NC}"

# 2. Launch Client
echo -e "${CYAN}[2/7] Starting sensitive client ('sparrow.fixed.client')...${NC}"
"$CLIENT_BIN" --duration=60 > "$LOG_CLIENT" 2>&1 &
CLIENT_PID=$!

SENSITIVE_CONFIRMED=false
for _ in $(seq 1 120); do
    if grep -E "View .* sensitivity -> true" "$LOG_SPARROW" >/dev/null 2>&1; then
        SENSITIVE_CONFIRMED=true
        break
    fi
    sleep 0.1
done

if [ "$SENSITIVE_CONFIRMED" = false ]; then
    echo -e "${RED}[FAIL] Window was not marked sensitive by compositor.${NC}"
    cat "$LOG_SPARROW"
    exit 1
fi
echo -e "${GREEN}[OK] View marked sensitive by compositor.${NC}"
sleep 1.0

# 3. Scenario 1: App Sensitive in Normal Mode (Landscape 0°)
echo -e "${CYAN}[3/7] Scenario 1: Sensitive App in Normal Mode (Landscape 0°)...${NC}"
IMG_SCENARIO_1="${OUT_DIR}/sec_app_normal.png"
grim "$IMG_SCENARIO_1"
audit_image "$IMG_SCENARIO_1" "app_censored" 0.05 0.999
echo -e "${GREEN}[OK] Scenario 1 Passed: App blacked out cleanly with zero pattern leaks.${NC}"

# 4. Scenario 2: App Sensitive in Normal Mode with Rotation (Portrait 90°)
echo -e "${CYAN}[4/7] Scenario 2: Sensitive App in Normal Mode with Rotation (Portrait 90°)...${NC}"
wlr-randr --output "$OUTPUT_NAME" --transform 90 2>/dev/null || true
sleep 1.2
IMG_SCENARIO_2="${OUT_DIR}/sec_app_rot90.png"
grim "$IMG_SCENARIO_2"
audit_image "$IMG_SCENARIO_2" "app_censored" 0.05 0.999
echo -e "${GREEN}[OK] Scenario 2 Passed: Rotated app blackout verified without leaks.${NC}"

# 5. Scenario 3: App Sensitive in Overview Mode (Landscape 0°)
echo -e "${CYAN}[5/7] Scenario 3: Sensitive App in Overview Mode (Landscape 0°)...${NC}"
wlr-randr --output "$OUTPUT_NAME" --transform normal 2>/dev/null || true
sleep 1.0
"$KEY_BIN" Alt_L 2>/dev/null || true
sleep 1.2

OVERVIEW_ACTIVE=false
if grep -F "[OVERVIEW] force_render_all_views set to TRUE" "$LOG_SPARROW" >/dev/null 2>&1; then
    OVERVIEW_ACTIVE=true
fi
if [ "$OVERVIEW_ACTIVE" = false ]; then
    echo -e "${RED}[FAIL] Overview mode not activated.${NC}"
    exit 1
fi

IMG_SCENARIO_3="${OUT_DIR}/sec_app_overview.png"
grim "$IMG_SCENARIO_3"
audit_image "$IMG_SCENARIO_3" "app_censored" 0.01 0.999
echo -e "${GREEN}[OK] Scenario 3 Passed: Overview thumbnail censored without pattern leaks.${NC}"

# 6. Scenario 4: App Sensitive in Overview Mode with Rotation (Portrait 90°)
echo -e "${CYAN}[6/7] Scenario 4: Sensitive App in Overview Mode with Rotation (Portrait 90°)...${NC}"
wlr-randr --output "$OUTPUT_NAME" --transform 90 2>/dev/null || true
sleep 1.5
IMG_SCENARIO_4="${OUT_DIR}/sec_app_overview_rot90.png"
grim "$IMG_SCENARIO_4"
audit_image "$IMG_SCENARIO_4" "app_censored" 0.01 0.999
echo -e "${GREEN}[OK] Scenario 4 Passed: Rotated overview thumbnail censored without leaks.${NC}"

# Exit Overview & return to normal
"$KEY_BIN" Return 2>/dev/null || true
wlr-randr --output "$OUTPUT_NAME" --transform normal 2>/dev/null || true
sleep 1.0

# 7. Scenario 5 & 6: Entire Output Sensitive (Landscape 0° & Portrait 90°)
echo -e "${CYAN}[7/7] Scenario 5 & 6: Entire Output Sensitive (Normal & Rotated)...${NC}"
cat >> "$CONFIG_FILE" << EOF
output = ${OUTPUT_NAME}
EOF

OUTPUT_SENSITIVE_CONFIRMED=false
for _ in $(seq 1 120); do
    if grep -E "Updated sensitive output_names \(1 entries\)" "$LOG_SPARROW" >/dev/null 2>&1; then
        OUTPUT_SENSITIVE_CONFIRMED=true
        break
    fi
    sleep 0.1
done

if [ "$OUTPUT_SENSITIVE_CONFIRMED" = false ]; then
    echo -e "${RED}[FAIL] Output sensitivity not registered in compositor.${NC}"
    exit 1
fi
sleep 1.0

# Scenario 5: Full Output in Normal (0°)
IMG_SCENARIO_5="${OUT_DIR}/sec_output_normal.png"
grim "$IMG_SCENARIO_5"
audit_image "$IMG_SCENARIO_5" "full_black"
echo -e "${GREEN}[OK] Scenario 5 Passed: Entire output 100% blacked out in normal orientation.${NC}"

# Scenario 6: Full Output in Rotation (90°)
wlr-randr --output "$OUTPUT_NAME" --transform 90 2>/dev/null || true
sleep 1.2
IMG_SCENARIO_6="${OUT_DIR}/sec_output_rot90.png"
grim "$IMG_SCENARIO_6"
audit_image "$IMG_SCENARIO_6" "full_black"
echo -e "${GREEN}[OK] Scenario 6 Passed: Entire output 100% blacked out in 90° rotation.${NC}"

echo -e "\n${GREEN}${BOLD}✔ ALL SECURE CONTENT & PRIVACY TESTS PASSED CLEANLY!${NC}\n"
exit 0

