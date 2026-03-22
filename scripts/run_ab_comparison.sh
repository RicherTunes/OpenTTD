#!/bin/bash
# ===============================================================
# Automated A/B Visual Comparison: Vanilla OpenTTD vs Modded
# ===============================================================
#
# Three-phase deterministic pipeline:
# 1. HEADLESS: Generate map with vanilla + save (no rendering timing)
# 2. VANILLA GUI: Load save + capture golden screenshots
# 3. MODDED GUI: Load SAME save + capture PP-off/PP-on/class/water
# 4. COMPARE: PSNR between vanilla and modded
#
# IMPORTANT: game_start.scr must be written to ALL search paths:
#   - build/Release/scripts/  (binary directory)
#   - build/scripts/          (build directory)
#   - <user_dir>/scripts/     (user directory)
#
# Usage: ./scripts/run_ab_comparison.sh [--seed 424242] [--timeout 60]
# ===============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VANILLA_DIR="$(cd "$ROOT_DIR/../OpenTTD-vanilla" 2>/dev/null && pwd)"

SEED="${SEED:-424242}"
TIMEOUT="${TIMEOUT:-60}"
SCROLL_TILE="32896"

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

write_all() {
    local content="$1"
    for loc in \
        "$VANILLA_DIR/build/Release/scripts" \
        "$VANILLA_DIR/build/scripts" \
        "$ROOT_DIR/build/Release/scripts" \
        "$ROOT_DIR/build/scripts" \
        "$OTTD_USER_DIR/scripts" \
        ; do
        mkdir -p "$loc" 2>/dev/null
        printf '%s\n' "$content" > "$loc/game_start.scr"
    done
}

cleanup() { write_all "; normal"; echo "; normal" > "$OTTD_USER_DIR/scripts/autoexec.scr"; }
trap cleanup EXIT

echo "=============================================="
echo "  A/B Comparison (seed=$SEED)"
echo "=============================================="

# Clean
rm -f "$SS_DIR"/golden_*.{png,bmp} "$SS_DIR"/modded_*.{png,bmp} 2>/dev/null
rm -f "$SAVE_DIR/ab_reference.sav" 2>/dev/null
echo "; minimal" > "$OTTD_USER_DIR/scripts/autoexec.scr"

# ================================================================
# Phase 1: Headless map generation + save (vanilla)
# ================================================================
echo ""
echo "--- Phase 1: Headless map gen + save ---"
write_all "save ab_reference"

cd "$VANILLA_DIR/build"
timeout 30 ./Release/openttd.exe -vnull:ticks=2000 -snull -mnull -G "$SEED" -g -Q 2>/dev/null || true
[ -f "$SAVE_DIR/ab_reference.sav" ] && echo "  Save: OK ($(stat -c %s "$SAVE_DIR/ab_reference.sav") bytes)" || { echo "  ERROR: save failed"; exit 1; }

# ================================================================
# Phase 2: Vanilla GUI - load save + golden screenshots
# ================================================================
echo ""
echo "--- Phase 2: Vanilla screenshots ---"
write_all "$(cat << SCRIPT
pause
scrollto instant $SCROLL_TILE
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
SCRIPT
)"

cd "$VANILLA_DIR/build"
timeout "$TIMEOUT" ./Release/openttd.exe -v win32-opengl -r 1280x720 -g "$SAVE_DIR/ab_reference.sav" -snull -mnull -Q 2>/dev/null || true
V=$(ls "$SS_DIR"/golden_*.png 2>/dev/null | wc -l)
echo "  Golden: $V screenshots"

# ================================================================
# Phase 3: Modded GUI - load SAME save + comparison screenshots
# ================================================================
echo ""
echo "--- Phase 3: Modded screenshots ---"
write_all "$(cat << SCRIPT
pause
scrollto instant $SCROLL_TILE
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
SCRIPT
)"

cd "$ROOT_DIR/build"
timeout "$TIMEOUT" ./Release/openttd.exe -v win32-opengl -r 1280x720 -g "$SAVE_DIR/ab_reference.sav" -snull -mnull -Q 2>/dev/null || true
M=$(ls "$SS_DIR"/modded_*.{png,bmp} 2>/dev/null | wc -l)
echo "  Modded: $M screenshots"

# ================================================================
# Phase 4: Compare
# ================================================================
echo ""
echo "--- Phase 4: PSNR Comparison ---"
echo ""

if command -v py >/dev/null 2>&1; then PYTHON=(py -3)
elif command -v python3 >/dev/null 2>&1; then PYTHON=(python3)
elif command -v python >/dev/null 2>&1; then PYTHON=(python)
else echo "WARNING: Python not found"; exit 0; fi

"${PYTHON[@]}" "$SCRIPT_DIR/compare_screenshots.py" \
    "$SS_DIR" --prefix golden_ 2>/dev/null || true

echo ""
echo "  Vanilla: $V  Modded: $M  Save: $(stat -c %s "$SAVE_DIR/ab_reference.sav" 2>/dev/null || echo '?') bytes"
echo "  Screenshots: $SS_DIR/"
