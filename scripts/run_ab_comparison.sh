#!/bin/bash
# ===============================================================
# Automated A/B Visual Comparison: Vanilla OpenTTD vs Modded
# ===============================================================
#
# Fully automated pipeline:
# 1. Generate map with vanilla OpenTTD (deterministic seed)
# 2. Capture golden screenshots from vanilla at 5 zoom levels
# 3. Generate SAME map with modded OpenTTD (same seed, features=OFF)
# 4. Capture PP-off + PP-on + classification + water screenshots
# 5. Compare with PSNR
#
# IMPORTANT: game_start.scr must be written to ALL of:
#   - build/Release/scripts/
#   - build/scripts/
#   - <user_dir>/scripts/
# because OpenTTD searches multiple paths.
#
# Usage: ./scripts/run_ab_comparison.sh [--seed 424242] [--timeout 60]
# ===============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VANILLA_DIR="$(cd "$ROOT_DIR/../OpenTTD-vanilla" 2>/dev/null && pwd)"

SEED="${SEED:-424242}"
TIMEOUT="${TIMEOUT:-60}"

while [[ $# -gt 0 ]]; do
    case $1 in --seed) SEED="$2"; shift 2;; --timeout) TIMEOUT="$2"; shift 2;; *) shift;; esac
done

VANILLA_EXE="$VANILLA_DIR/build/Release/openttd.exe"
MODDED_EXE="$ROOT_DIR/build/Release/openttd.exe"
for exe in "$VANILLA_EXE" "$MODDED_EXE"; do
    [ -f "$exe" ] || { echo "ERROR: $exe not found"; exit 1; }
done

# Find OpenTTD user directory
shopt -s nullglob
OTTD_USER_DIR=""
for p in "${USERPROFILE:-}"/OneDrive*/Documents/OpenTTD "${USERPROFILE:-}"/Documents/OpenTTD "${APPDATA:-}"/OpenTTD; do
    for d in $p; do [ -d "$d" ] && { OTTD_USER_DIR="$d"; break 2; }; done
done
shopt -u nullglob
[ -z "$OTTD_USER_DIR" ] && { echo "ERROR: OpenTTD user dir not found"; exit 1; }

SS_DIR="$OTTD_USER_DIR/screenshot"
SAVE_DIR="$OTTD_USER_DIR/save"
mkdir -p "$SS_DIR" "$SAVE_DIR"

OUTPUT_DIR="$ROOT_DIR/ab_comparison_output"
rm -rf "$OUTPUT_DIR"; mkdir -p "$OUTPUT_DIR/golden" "$OUTPUT_DIR/modded"

write_script() {
    local content="$1"
    for loc in \
        "$VANILLA_DIR/build/Release/scripts" \
        "$VANILLA_DIR/build/scripts" \
        "$ROOT_DIR/build/Release/scripts" \
        "$ROOT_DIR/build/scripts" \
        "$OTTD_USER_DIR/scripts" \
        ; do
        mkdir -p "$loc" 2>/dev/null
        echo "$content" > "$loc/game_start.scr"
    done
}

echo "=============================================="
echo "  A/B Comparison: seed=$SEED timeout=${TIMEOUT}s"
echo "  User dir: $OTTD_USER_DIR"
echo "=============================================="

# Force small map via autoexec
cat > "$OTTD_USER_DIR/scripts/autoexec.scr" << 'EOF'
setting_newgame game_creation.map_x 8
setting_newgame game_creation.map_y 8
EOF

# Clean
rm -f "$SS_DIR"/golden_*.{png,bmp} "$SS_DIR"/modded_*.{png,bmp} 2>/dev/null

# === STEP 1: Vanilla ===
echo ""
echo "=== Step 1: Vanilla golden screenshots ==="
write_script "$(cat << 'EOF'
pause
scrollto instant 32896
save ab_reference
zoomto 0
screenshot viewport no_con golden_01_zoom_in4x
zoomto 1
screenshot viewport no_con golden_02_zoom_in2x
zoomto 2
screenshot viewport no_con golden_03_zoom_normal
zoomto 3
screenshot viewport no_con golden_04_zoom_out2x
zoomto 4
screenshot viewport no_con golden_05_zoom_out4x
screenshot minimap golden_06_minimap
exit
EOF
)"

cd "$VANILLA_DIR/build"
timeout "$TIMEOUT" ./Release/openttd.exe -v win32-opengl -r 1280x720 -G "$SEED" -g -snull -mnull -Q 2>/dev/null || true

V=0
for f in "$SS_DIR"/golden_*.png; do [ -f "$f" ] && { cp "$f" "$OUTPUT_DIR/golden/"; V=$((V+1)); }; done
echo "  Golden: $V screenshots"
[ -f "$SAVE_DIR/ab_reference.sav" ] && echo "  Save: OK" || echo "  Save: MISSING"

# === STEP 2: Modded ===
echo ""
echo "=== Step 2: Modded screenshots ==="
write_script "$(cat << 'EOF'
pause
scrollto instant 32896
pp off
zoomto 0
screenshot viewport no_con modded_01_ppoff_zoom_in4x
zoomto 1
screenshot viewport no_con modded_02_ppoff_zoom_in2x
zoomto 2
screenshot viewport no_con modded_03_ppoff_zoom_normal
zoomto 3
screenshot viewport no_con modded_04_ppoff_zoom_out2x
zoomto 4
screenshot viewport no_con modded_05_ppoff_zoom_out4x
screenshot minimap modded_06_ppoff_minimap
zoomto 2
pp on
pp enable fxaa
pp set sharpening 50
pp enable vignette
pp_screenshot modded_07_ppon_normal
pp debug_class on
pp_screenshot modded_08_debug_class
pp debug_class off
pp enable water
pp_screenshot modded_09_water_on
pp enable night
pp_screenshot modded_10_water_night
pp off
exit
EOF
)"

cd "$ROOT_DIR/build"
timeout "$TIMEOUT" ./Release/openttd.exe -v win32-opengl -r 1280x720 -G "$SEED" -g -snull -mnull -Q 2>/dev/null || true

M=0
for f in "$SS_DIR"/modded_*.{png,bmp}; do [ -f "$f" ] && { cp "$f" "$OUTPUT_DIR/modded/"; M=$((M+1)); }; done
echo "  Modded: $M screenshots"

# Cleanup
echo "; normal" > "$OTTD_USER_DIR/scripts/autoexec.scr"
write_script "; normal"

# === STEP 3: Compare ===
echo ""
echo "=== Step 3: Results ==="
echo "  Output: $OUTPUT_DIR/"
echo "  Golden: $V  Modded: $M"
echo ""

if command -v py >/dev/null 2>&1; then
    py -3 "$SCRIPT_DIR/compare_screenshots.py" "$OUTPUT_DIR/golden" --prefix golden_
fi
