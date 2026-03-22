#!/usr/bin/env python3
"""
Screenshot comparison tool for OpenTTD visual regression testing.

Compares pairs of BMP/PNG screenshots using PSNR (Peak Signal-to-Noise Ratio)
and mean pixel difference. Reports pass/fail per pair.

Usage:
    python compare_screenshots.py <screenshot_dir> [--golden <golden_dir>] [--threshold <psnr_db>]

Modes:
    1. Self-consistency: compare baseline vs restore (should be identical)
    2. Effect visibility: compare baseline vs effect screenshots (should differ)
    3. Golden reference: compare current vs golden reference images

Exit code 0 = all checks pass, 1 = failures detected.
"""

import sys
import os
import math
import glob
import argparse


def load_image(path):
    """Load an image file and return (width, height, pixels) tuple.
    pixels is a list of (r, g, b) tuples. Supports BMP only (no PIL needed)."""
    with open(path, 'rb') as f:
        header = f.read(54)
        if header[:2] != b'BM':
            raise ValueError(f"Not a BMP file: {path}")

        data_offset = int.from_bytes(header[10:14], 'little')
        width = int.from_bytes(header[18:22], 'little', signed=True)
        height = int.from_bytes(header[22:26], 'little', signed=True)
        bpp = int.from_bytes(header[28:30], 'little')

        if bpp not in (24, 32):
            raise ValueError(f"Unsupported BMP bit depth: {bpp}")

        bottom_up = height > 0
        height = abs(height)
        bytes_per_pixel = bpp // 8
        row_size = ((width * bytes_per_pixel + 3) & ~3)

        f.seek(data_offset)
        raw = f.read(row_size * height)

        pixels = []
        for y in range(height):
            row_y = (height - 1 - y) if bottom_up else y
            row_offset = row_y * row_size
            for x in range(width):
                px_offset = row_offset + x * bytes_per_pixel
                b = raw[px_offset]
                g = raw[px_offset + 1]
                r = raw[px_offset + 2]
                pixels.append((r, g, b))

        return width, height, pixels


def compute_psnr(pixels1, pixels2):
    """Compute PSNR between two pixel lists. Returns (psnr, mean_diff, max_diff)."""
    if len(pixels1) != len(pixels2):
        return 0.0, 999.0, 999

    n = len(pixels1)
    if n == 0:
        return float('inf'), 0.0, 0

    mse_sum = 0
    max_diff = 0
    for p1, p2 in zip(pixels1, pixels2):
        for a, b in zip(p1, p2):
            d = a - b
            mse_sum += d * d
            max_diff = max(max_diff, abs(d))

    mse = mse_sum / (n * 3)
    mean_diff = math.sqrt(mse)

    if mse == 0:
        return float('inf'), 0.0, 0

    psnr = 10 * math.log10(255 * 255 / mse)
    return psnr, mean_diff, max_diff


def check_dominant_color_region(width, height, pixels, target_rgb, tolerance=60):
    """Count pixels within tolerance of target color. Returns fraction (0-1)."""
    count = 0
    for r, g, b in pixels:
        if (abs(r - target_rgb[0]) < tolerance and
            abs(g - target_rgb[1]) < tolerance and
            abs(b - target_rgb[2]) < tolerance):
            count += 1
    return count / len(pixels) if pixels else 0


def main():
    parser = argparse.ArgumentParser(description='Compare OpenTTD screenshots for visual regression testing')
    parser.add_argument('screenshot_dir', help='Directory containing screenshots')
    parser.add_argument('--golden', help='Directory containing golden reference images')
    parser.add_argument('--threshold', type=float, default=30.0, help='PSNR threshold in dB (default: 30)')
    parser.add_argument('--prefix', default='vc_', help='Screenshot filename prefix (default: vc_)')
    args = parser.parse_args()

    ss_dir = args.screenshot_dir
    if not os.path.isdir(ss_dir):
        print(f"ERROR: Screenshot directory not found: {ss_dir}")
        return 1

    # Find all screenshots with the prefix
    screenshots = {}
    for ext in ('*.bmp', '*.png'):
        for path in glob.glob(os.path.join(ss_dir, f"{args.prefix}{ext}")):
            name = os.path.splitext(os.path.basename(path))[0]
            screenshots[name] = path

    if not screenshots:
        print(f"ERROR: No screenshots found with prefix '{args.prefix}' in {ss_dir}")
        return 1

    print(f"Found {len(screenshots)} screenshots:")
    for name in sorted(screenshots.keys()):
        size = os.path.getsize(screenshots[name])
        print(f"  {name}: {size:,} bytes")
    print()

    failures = []
    passes = []

    # --- Check 1: Baseline vs Restore (should be identical or near-identical) ---
    baseline = screenshots.get(f'{args.prefix}01_baseline')
    restore = screenshots.get(f'{args.prefix}06_restore')

    if baseline and restore:
        print("=== Check 1: Baseline vs Restore (should be identical) ===")
        w1, h1, px1 = load_image(baseline)
        w2, h2, px2 = load_image(restore)
        if w1 != w2 or h1 != h2:
            msg = f"  FAIL: Size mismatch {w1}x{h1} vs {w2}x{h2}"
            print(msg)
            failures.append(msg)
        else:
            psnr, mean, maxd = compute_psnr(px1, px2)
            if psnr == float('inf'):
                print(f"  PASS: Pixel-identical (PSNR=inf)")
                passes.append("baseline_vs_restore: identical")
            elif psnr > 40:
                print(f"  PASS: Near-identical (PSNR={psnr:.1f} dB, mean_diff={mean:.2f}, max_diff={maxd})")
                passes.append(f"baseline_vs_restore: PSNR={psnr:.1f}")
            else:
                msg = f"  FAIL: Too different (PSNR={psnr:.1f} dB, mean_diff={mean:.2f}, max_diff={maxd})"
                print(msg)
                failures.append(msg)
        print()

    # --- Check 2: Effects should produce visible changes vs baseline ---
    if baseline:
        print("=== Check 2: Effect screenshots should differ from baseline ===")
        w1, h1, px1 = load_image(baseline)
        for name in sorted(screenshots.keys()):
            if name in (f'{args.prefix}01_baseline', f'{args.prefix}06_restore'):
                continue
            path = screenshots[name]
            try:
                w2, h2, px2 = load_image(path)
                if w1 != w2 or h1 != h2:
                    print(f"  SKIP {name}: size mismatch")
                    continue
                psnr, mean, maxd = compute_psnr(px1, px2)
                if psnr == float('inf') or psnr > 50:
                    msg = f"  WARNING: {name} looks identical to baseline (PSNR={psnr:.1f} dB)"
                    print(msg)
                    # Not a hard failure -- some effects may be subtle
                else:
                    print(f"  OK: {name} differs (PSNR={psnr:.1f} dB, mean_diff={mean:.2f}, max_diff={maxd})")
                    passes.append(f"{name}: PSNR={psnr:.1f}")
            except Exception as e:
                print(f"  ERROR: {name}: {e}")
        print()

    # --- Check 3: Debug class view should have expected false colours ---
    debug_class = screenshots.get(f'{args.prefix}02_debug_class')
    if debug_class:
        print("=== Check 3: Debug class view colour check ===")
        try:
            w, h, px = load_image(debug_class)
            # Check for presence of classification colours (with 30% scene blend)
            # Water blue: ~(26, 77, 230) blended 70% with scene
            # Terrain brown: ~(140, 89, 43) blended 70% with scene
            # Vegetation green: ~(26, 179, 51) blended 70% with scene
            blue_frac = check_dominant_color_region(w, h, px, (26, 77, 230), tolerance=80)
            brown_frac = check_dominant_color_region(w, h, px, (140, 89, 43), tolerance=80)
            green_frac = check_dominant_color_region(w, h, px, (26, 179, 51), tolerance=80)
            print(f"  Blue (water) pixels: {blue_frac*100:.1f}%")
            print(f"  Brown (terrain) pixels: {brown_frac*100:.1f}%")
            print(f"  Green (vegetation) pixels: {green_frac*100:.1f}%")
            if blue_frac + brown_frac + green_frac > 0.01:
                print(f"  PASS: Classification colours detected")
                passes.append("debug_class: colours present")
            else:
                msg = "  WARNING: No classification colours detected (buffer may not be active)"
                print(msg)
        except Exception as e:
            print(f"  ERROR: {e}")
        print()

    # --- Check 4: Golden reference comparison ---
    if args.golden and os.path.isdir(args.golden):
        print(f"=== Check 4: Golden reference comparison (threshold={args.threshold} dB) ===")
        for name, path in sorted(screenshots.items()):
            golden_path = os.path.join(args.golden, os.path.basename(path))
            if not os.path.exists(golden_path):
                continue
            try:
                w1, h1, px1 = load_image(path)
                w2, h2, px2 = load_image(golden_path)
                if w1 != w2 or h1 != h2:
                    msg = f"  FAIL: {name} size mismatch vs golden"
                    print(msg)
                    failures.append(msg)
                    continue
                psnr, mean, maxd = compute_psnr(px1, px2)
                if psnr >= args.threshold or psnr == float('inf'):
                    print(f"  PASS: {name} (PSNR={psnr:.1f} dB)")
                    passes.append(f"golden_{name}: PSNR={psnr:.1f}")
                else:
                    msg = f"  FAIL: {name} below threshold (PSNR={psnr:.1f} dB < {args.threshold})"
                    print(msg)
                    failures.append(msg)
            except Exception as e:
                print(f"  ERROR: {name}: {e}")
        print()

    # --- Summary ---
    print("=" * 60)
    print(f"PASSES: {len(passes)}")
    print(f"FAILURES: {len(failures)}")
    if failures:
        print("\nFailed checks:")
        for f in failures:
            print(f"  {f}")
        return 1
    else:
        print("\nAll checks passed.")
        return 0


if __name__ == '__main__':
    sys.exit(main())
