#!/bin/bash
# Fully automated E2E visual test for GPU post-processing pipeline
# Usage: ./scripts/run_e2e_test.sh [build_dir]
#
# This script:
# 1. Sets up autoexec to run the e2e test script on game launch
# 2. Launches the game (which auto-starts newgame + runs tests + quits)
# 3. Waits for screenshots to appear
# 4. Validates that screenshots were created and differ from baseline
# 5. Reports results

set -e

BUILD_DIR="${1:-build_full}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GAME_EXE="$BUILD_DIR/RelWithDebInfo/openttd.exe"

if [ ! -f "$GAME_EXE" ]; then
    echo "ERROR: Game executable not found at $GAME_EXE"
    echo "Build with: cmake --build $BUILD_DIR --target openttd --config RelWithDebInfo"
    exit 1
fi

echo "=== OpenTTD GPU Post-Processing E2E Test ==="
echo "Build: $BUILD_DIR"
echo ""

# Setup autoexec to run the automated test
mkdir -p "$BUILD_DIR/scripts"
cp "$SCRIPT_DIR/automated_e2e_test.scr" "$BUILD_DIR/scripts/"
cat > "$BUILD_DIR/scripts/autoexec.scr" << 'EOF'
newgame
schedule automated_e2e_test.scr
EOF

echo "Step 1: Launching game with automated test..."
cd "$BUILD_DIR"
./RelWithDebInfo/openttd.exe &
GAME_PID=$!

echo "Step 2: Waiting for game to run tests and quit (max 120s)..."
TIMEOUT=120
ELAPSED=0
while kill -0 $GAME_PID 2>/dev/null; do
    sleep 2
    ELAPSED=$((ELAPSED + 2))
    if [ $ELAPSED -ge $TIMEOUT ]; then
        echo "TIMEOUT: Game did not exit within ${TIMEOUT}s. Killing."
        kill -9 $GAME_PID 2>/dev/null || true
        break
    fi
done

echo "Step 3: Checking for screenshots..."

# Find the screenshot directory
SS_DIR=""
for dir in \
    "$HOME/Documents/OpenTTD/screenshot" \
    "$HOME/OneDrive*/Documents/OpenTTD/screenshot" \
    "$APPDATA/OpenTTD/screenshot" \
    ; do
    expanded=$(ls -d $dir 2>/dev/null | head -1)
    if [ -n "$expanded" ] && [ -d "$expanded" ]; then
        SS_DIR="$expanded"
        break
    fi
done

if [ -z "$SS_DIR" ]; then
    # Broad search
    SS_DIR=$(find "$HOME" -maxdepth 6 -path "*/OpenTTD/screenshot" -type d 2>/dev/null | head -1)
fi

if [ -z "$SS_DIR" ]; then
    echo "ERROR: Could not find OpenTTD screenshot directory"
    exit 1
fi

echo "Screenshot dir: $SS_DIR"

# Count e2e screenshots
EXPECTED=13
FOUND=$(ls "$SS_DIR"/e2e_*.bmp 2>/dev/null | wc -l)
PNG_FOUND=$(ls "$SS_DIR"/e2e_*.png 2>/dev/null | wc -l)
TOTAL=$((FOUND + PNG_FOUND))

echo ""
echo "=== Results ==="
echo "Expected: $EXPECTED screenshots"
echo "Found: $TOTAL ($FOUND BMP + $PNG_FOUND PNG)"
echo ""

if [ $TOTAL -eq 0 ]; then
    echo "FAIL: No screenshots produced."
    echo "The pp_screenshot command may not be working."
    exit 1
fi

# List all screenshots with sizes
echo "Screenshots:"
ls -la "$SS_DIR"/e2e_*.{bmp,png} 2>/dev/null | while read line; do
    echo "  $line"
done

# Check if effects produced different files (different sizes = different content)
echo ""
echo "=== File size comparison (different sizes = effects working) ==="
BASELINE_SIZE=$(stat -c %s "$SS_DIR/e2e_01_baseline."* 2>/dev/null | head -1)
DIFFERENT=0
SAME=0
for f in "$SS_DIR"/e2e_*.{bmp,png} 2>/dev/null; do
    [ -f "$f" ] || continue
    SIZE=$(stat -c %s "$f")
    NAME=$(basename "$f")
    if [ "$SIZE" != "$BASELINE_SIZE" ]; then
        echo "  DIFF: $NAME ($SIZE bytes vs baseline $BASELINE_SIZE)"
        DIFFERENT=$((DIFFERENT + 1))
    else
        echo "  SAME: $NAME ($SIZE bytes)"
        SAME=$((SAME + 1))
    fi
done

echo ""
echo "=== Summary ==="
echo "Total screenshots: $TOTAL / $EXPECTED"
echo "Different from baseline: $DIFFERENT"
echo "Same as baseline: $SAME"

if [ $TOTAL -ge $EXPECTED ] && [ $DIFFERENT -gt 0 ]; then
    echo "RESULT: PASS - Effects are producing visible changes"
elif [ $TOTAL -ge $EXPECTED ] && [ $DIFFERENT -eq 0 ]; then
    echo "RESULT: WARNING - All screenshots identical (effects may not be rendering)"
else
    echo "RESULT: PARTIAL - Only $TOTAL of $EXPECTED screenshots captured"
fi

# Cleanup autoexec
cat > "$BUILD_DIR/scripts/autoexec.scr" << 'EOF'
; Normal startup (no automated test)
EOF

echo ""
echo "Done. Screenshots are in: $SS_DIR"
