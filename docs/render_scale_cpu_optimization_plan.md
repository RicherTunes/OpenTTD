# Render Scale CPU Optimization Plan

**Date:** 2026-03-22
**Goal:** Enable render_scale < 100% to actually reduce CPU blitter work while keeping UI at full resolution.

---

## Problem Statement

Render scaling (50-100%) is intended to reduce CPU blitter work by rendering at lower resolution, then GPU-upscaling to display. The current implementation renders the CPU blitter at full resolution — the render_scale setting only affects FBO dimensions, providing zero CPU savings.

The previous attempt to set `_screen` to render resolution broke the UI coordinate system (black bars, mouse misalignment) because `_screen` is used globally for UI layout, viewport bounds, mouse mapping, and dirty rect tracking.

## Architecture: Split-Resolution Rendering

The solution: **render the viewport (game world) at reduced resolution but keep UI windows at full resolution.** This is the standard approach in every modern game engine.

### How OpenTTD's Drawing Works

```
UpdateWindows()
  → DrawDirtyBlocks()
    → RedrawScreenRect(left, top, right, bottom)
      → DrawOverlappedWindowForAll(left, top, right, bottom)
        → For each visible window in the dirty rect:
          → Window::OnPaint()
            → If window has viewport: Window::DrawViewport()
              → ViewportDraw() → ViewportDoDraw()  ← THE EXPENSIVE PART
            → DrawWidgets() ← UI elements (cheap)
```

The key insight: `ViewportDoDraw()` is the expensive CPU operation. It iterates tiles, sorts sprites, and draws thousands of sprites. UI widget drawing is comparatively cheap.

### The Plan

**Phase 1: Viewport-Only Scale Reduction**

1. Keep `_screen` at display resolution (current behavior — UI works correctly).
2. Create a **separate** `DrawPixelInfo _viewport_dpi` for viewport rendering at reduced resolution.
3. In `Window::DrawViewport()`, replace `_cur_dpi` with a scaled version:
   - Allocate a render-resolution buffer for the viewport area only
   - Set `_cur_dpi` to point at this smaller buffer with adjusted pitch
   - The blitter draws sprites at the reduced resolution
   - After viewport draw completes, the reduced-resolution viewport pixels are copied back to `_screen` at display resolution (or left for the GPU to upscale)
4. The FBO pipeline picks up the mixed-resolution framebuffer and applies upscaling only to the viewport area.

**Approach A: Full-Frame Reduced Resolution (Simpler)**

Instead of per-viewport scaling, render the entire `_screen` at reduced resolution but **remap mouse/UI coordinates** at the boundary:

```
_screen.width = render_w (reduced)
_screen.height = render_h (reduced)
_screen.pitch = render_pitch

Mouse input: scale_x = display_w / render_w
  mouse_x_game = mouse_x_display * render_w / display_w
  mouse_y_game = mouse_y_display * render_h / display_h

UI windows: position and size scaled proportionally
  Window appears at (x * render_w/display_w, y * render_h/display_h)
```

This is simpler but makes the UI fuzzy at low render_scale.

**Approach B: Dual-DPI Rendering (Better Quality)**

- `_screen` stays at display resolution for UI
- The main viewport window's `DrawViewport()` is intercepted:
  - A reduced-resolution scratch buffer is allocated for the viewport area
  - ViewportDoDraw uses this scratch buffer
  - After drawing, the scratch buffer is uploaded to a separate GL texture
  - The FBO pipeline upscales this texture and composites it with the UI

This gives: sharp UI + reduced-resolution game world + GPU upscaling = best of both worlds.

**Approach C: Zoom-Level Trick (Simplest, No Architecture Change)**

OpenTTD already has zoom levels. Reducing render_scale by 50% is equivalent to zooming OUT one level but displaying at the same viewport size. The blitter already handles zoom via `ScaleByZoom()`.

Instead of changing buffer sizes, we could:
- Increase the viewport's zoom level by one step when render_scale < 100%
- Keep the viewport pixel dimensions the same
- The blitter draws fewer tiles (zoomed out) = fewer sprites = faster
- The GPU upscale shader provides quality improvement over the raw zoomed-out pixels

This gives REAL CPU savings with ZERO architecture changes. The trade-off: the game appears slightly zoomed out, but the upscale shader restores quality.

## Recommendation

**Approach C (Zoom-Level Trick)** for immediate implementation — zero risk, real savings:
- Modify `Window::DrawViewport()` to temporarily adjust viewport zoom when render_scale < 100%
- The existing zoom infrastructure handles everything
- CPU draws fewer sprites at the zoomed-out level
- GPU EASU/FSR upscale restores sharpness
- UI stays at full resolution and full size
- Mouse coordinates are unaffected (they go through viewport zoom mapping which already works)

**Approach B (Dual-DPI)** for future work — gives true resolution independence but requires a scratch buffer and texture upload for the viewport.

## Risk Assessment

| Approach | Complexity | CPU Savings | UI Quality | Risk |
|----------|-----------|-------------|------------|------|
| A (full-frame reduced) | Medium | High (56% at 75%) | Fuzzy UI | Mouse/window bugs |
| B (dual-DPI viewport) | High | High (viewport only) | Full-res UI | Architecture change |
| C (zoom-level trick) | **Low** | Medium (fewer sprites) | Full-res UI | **Near zero** |

## Implementation Steps for Approach C

1. In `Window::DrawViewport()`, before `ViewportDraw()`:
   ```cpp
   ZoomLevel saved_zoom = this->viewport->zoom;
   if (_video_post_processing && _video_render_scale < 100 && _video_render_scale >= 50) {
       /* Reduce effective zoom to render fewer sprites. */
       if (_video_render_scale <= 50) this->viewport->zoom = std::min(saved_zoom + 2, ZoomLevel::Out8x);
       else if (_video_render_scale <= 75) this->viewport->zoom = std::min(saved_zoom + 1, ZoomLevel::Out4x);
       /* Virtual dimensions automatically scale with zoom. */
   }
   ViewportDraw(...);
   this->viewport->zoom = saved_zoom;  /* Restore original zoom. */
   ```
2. The EASU/FSR upscale shader compensates for the additional zoom-out, restoring perceived detail.
3. Tests: verify zoom level is restored after DrawViewport, verify mouse coordinates still work.

## Performance Estimate

At 75% render_scale with zoom+1:
- Viewport shows ~33% more area but draws at lower detail level
- Sprite count typically decreases 30-50% (fewer tiles visible at detail level)
- CPU blitter work: ~50-70% of full resolution
- Combined with EASU upscale: perceived quality close to full resolution

At 50% render_scale with zoom+2:
- Viewport shows ~4x more area at lowest detail
- Sprite count decreases 60-80%
- CPU blitter work: ~20-40% of full resolution
- EASU upscale has more work but still fast on GPU
