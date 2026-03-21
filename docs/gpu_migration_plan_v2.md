# OpenTTD GPU Migration Plan: Enabling DLSS & FSR

**Date:** 2026-03-21
**Version:** 2.0 (Post-adversarial review -- hardened)
**Goal:** Migrate OpenTTD's rendering pipeline to enable DLSS and FSR integration (version-agnostic), improving visual quality while maintaining the game's broad hardware compatibility.

**Supersedes:** `gpu_migration_plan.md` (v1.0)

---

## 0. What Changed From v1.0

The v1.0 plan was subjected to four hostile adversarial reviews (technical feasibility, architecture, performance, community/licensing). The reviews identified **8 CRITICAL** and **12 HIGH** severity issues. Major changes in v2.0:

| v1.0 Problem | v2.0 Fix |
|-------------|----------|
| Per-pixel motion vectors written inside blitter inner loops | **Draw-command recording** at GfxBlitter level + GPU-side MV rasterization. Zero blitter modifications. |
| 14 blitter variants needed modification (SSE loops incompatible) | Blitters completely untouched. MV data captured as (rect, dx, dy) command list. |
| macOS broken (OpenGL capped at 4.1, compute needs 4.3+) | Phase 2 uses **Metal compute** on macOS via SDL_GPU or direct Metal backend. Explicit macOS strategy. |
| FSR 2 on OpenGL 3x slower on NVIDIA (80% of market) | Phase 2 scoped as **4-week research prototype** with quality gate. Full commitment only if proven. |
| No quality gates between phases | **Hard kill criteria** between every phase. Phase 3 requires Phase 2 to beat FSR 1 in blind tests. |
| GPLv2 + DLSS licensing dismissed as "open question" | DLSS integration **must** be a dynamically-loaded optional plugin with C ABI boundary. Architectured into Phase 3 design. |
| 3 new video drivers (12 total, untestable matrix) | **Composition over inheritance**: render backends compose inside existing drivers. Driver count unchanged. |
| RenderBackend interface incomplete | Interface audited against all OpenGLBackend public methods. Cursor, animation buffer, palette, dirty rects included. |
| Time estimates 2-4x too optimistic | Revised estimates with explicit uncertainty ranges. Phase 2 split into prototype + full implementation. |
| No performance budgets | **Hard performance budgets** with specific GPU/CPU targets and measurement methodology. |
| Frame Generation useless for fixed tick rate | Frame Generation **removed from scope**. Only Super Resolution and spatial upscaling in scope. |
| Per-pixel MV doubles CPU write bandwidth, destroys cache | Draw-command recording produces ~5KB/frame command list, not 8MB/frame pixel buffer. |
| No power/thermal consideration | All upscaling defaults to OFF. Power profile settings added. |

---

## 1. Technology Targets

### DLSS 4.5 Super Resolution (Phase 3)

| Property | Detail |
|----------|--------|
| **Function** | Temporal upscaling from lower to higher resolution |
| **GPU Support** | All GeForce RTX (20/30/40/50 series) |
| **API** | DX11, DX12, Vulkan via Streamline SDK |
| **Inputs** | Color buffer, motion vectors, depth buffer, jitter offsets |
| **Licensing** | MIT headers, proprietary inference DLL -- requires plugin architecture for GPLv2 |

### FSR 2.x (Phase 2) / FSR 3.x (Phase 3)

| Property | FSR 2 | FSR 3 |
|----------|-------|-------|
| **Function** | Temporal upscaling | Temporal upscaling + frame gen |
| **GPU Support** | Any GPU with compute | Any GPU with compute |
| **API** | DX12, Vulkan, **OpenGL (community port)** | DX12, Vulkan |
| **Inputs** | Color, motion vectors, depth, jitter | Same |
| **Licensing** | MIT -- GPLv2 compatible | MIT |

### FSR 1 / CAS (Phase 1)

| Property | Detail |
|----------|--------|
| **Function** | Spatial upscaling + sharpening (single-frame, no temporal data) |
| **GPU Support** | Any GPU with OpenGL fragment shaders |
| **API** | Any (simple GLSL shader) |
| **Inputs** | Color buffer only |
| **Licensing** | MIT |

### Removed from Scope

- **DLSS Frame Generation** -- OpenTTD's fixed tick rate (33fps game state) means generated frames are identical to real frames. No visual benefit, wasted GPU compute.
- **DLSS 5 Neural Rendering** -- Photorealistic enhancement of 3D content. Fundamentally wrong for 2D pixel art. Not released until Fall 2026.
- **FSR 4** -- RX 9000 exclusive, DX12 only, ML-based. Too vendor-locked. Players with RX 9000 get FSR 4 via AMD driver overlay anyway.

---

## 2. Revised Strategy: Three Phases with Hard Gates

```
Phase 1: Post-Processing Foundation + FSR 1         [4-6 weeks]  COMMITTED
    FBO pipeline, spatial upscaling, sharpening.
    No motion vectors. Works TODAY on existing OpenGL 3.2+.

    ──── Quality Gate 1 ────
    "Does the FBO infrastructure work reliably across platforms?"

Phase 2a: Motion Vector Prototype                    [4 weeks]    GATED
    Draw-command recording + GPU MV rasterization.
    FSR 2 proof-of-concept on ONE platform.
    Quality comparison vs FSR 1.

    ──── Quality Gate 2 (HARD KILL) ────
    "Does FSR 2 with our motion vectors produce visually superior
     results to FSR 1 at 67% render scale? Blind test, N>20."

Phase 2b: Full FSR 2 Integration                     [8-12 weeks] GATED
    Production-quality FSR 2 across platforms.
    Only if Gate 2 passes.

    ──── Quality Gate 3 ────
    "Is there community demand for DLSS? Is GPLv2 plugin
     architecture validated?"

Phase 3: Vulkan/DX11 Backend + DLSS                  [16-30 weeks] GATED
    Only if Gate 3 passes AND Phase 2 demonstrates value.
```

---

## 3. Phase 1: Post-Processing Foundation + FSR 1

**Duration:** 4-6 weeks
**Risk:** Low
**Status:** COMMITTED (no gate required)

### 3.1 Objectives

- Add FBO-based off-screen rendering to OpenGL backend
- Implement render scaling (render at lower resolution, display at window resolution)
- Add FSR 1 spatial upscaling as GLSL fragment shader
- Add CAS (Contrast Adaptive Sharpening) post-process
- User settings in GUI, all defaulting to OFF

### 3.2 Technical Design

Identical to v1.0 Section 2.2. No changes required -- this was the one section all reviewers agreed was sound.

**Modified render flow:**
```
CPU blitter -> PBO -> vid_texture (at render_scale resolution)
    -> fullscreen quad -> scene_fbo (render resolution)
    -> upscale pass -> pp_fbo (display resolution)
    -> sharpen pass -> screen
```

### 3.3 Platform Coverage

| Platform | Support |
|----------|---------|
| Windows (Win32 OpenGL) | Full -- GL 3.2+ guaranteed |
| Linux (SDL2 OpenGL) | Full -- GL 3.2+ core profile |
| macOS (Cocoa OpenGL) | Full -- GL 3.2 core available |
| Software backends | No post-processing (graceful no-op) |

### 3.4 Render Scale Guidelines

| Display | Min Scale | Recommended Scale | Notes |
|---------|-----------|-------------------|-------|
| 1080p | 75% (810p) | 85%+ | Below 75% quality degrades noticeably |
| 1440p | 67% (960p) | 75%+ | Good sweet spot for FSR 1 |
| 4K | 50% (1080p) | 67%+ | FSR 1 acceptable at 67% |

**CPU savings from render scaling:**
- 75% scale: CPU blitter work reduced ~44% (pixel count: 56% of native)
- 67% scale: CPU blitter work reduced ~55% (pixel count: 45% of native)
- 50% scale: CPU blitter work reduced ~75% (pixel count: 25% of native)

### 3.5 Files Modified

| File | Change |
|------|--------|
| `src/video/opengl.h` | Add FBO/texture members, post-process methods |
| `src/video/opengl.cpp` | FBO init/resize/destroy, modified Paint(), post-process chain |
| `src/table/opengl_shader.h` | FSR 1 EASU + RCAS and CAS shader sources |
| `src/settings_type.h` | Add render_scale, sharpening, upscale_mode |
| `src/table/settings/gui_settings.ini` | Setting definitions |

**Estimated LOC:** ~540 lines (unchanged from v1.0)

### 3.6 Quality Gate 1

- [ ] FBO rendering produces pixel-identical output at 100% scale (no-op pass)
- [ ] FSR 1 at 75% scale is visually acceptable on 1080p
- [ ] CAS sharpening works at all zoom levels
- [ ] No performance regression at 100% scale (< 0.5ms overhead)
- [ ] Works on Windows, Linux, macOS

---

## 4. Phase 2a: Motion Vector Prototype

**Duration:** 4 weeks
**Risk:** Medium-High (unproven concept)
**Status:** GATED on Phase 1 complete

### 4.1 Core Insight: Draw-Command Recording

**All four adversarial reviewers independently concluded: do not modify the blitter.**

Instead of writing per-pixel motion vectors inside the blitter's inner loops (which destroys CPU cache performance, breaks SIMD optimizations, and requires modifying 14+ blitter variants), motion vectors are generated by:

1. **Recording draw commands** at the `GfxBlitter()` / `DrawSpriteViewport()` level
2. **Rasterizing motion vectors on the GPU** from the recorded command list

This approach has zero blitter modifications, works with all blitter variants (including 8bpp and SSE), and costs ~5KB/frame instead of 8MB/frame.

### 4.2 Draw-Command Recording Architecture

```cpp
/* Recorded per sprite draw call, NOT per pixel */
struct DrawCommand {
    Rect screen_rect;    /* Bounding box on screen (after clipping) */
    int16_t dx, dy;      /* Screen-space motion since last frame (1/8 px fixed-point) */
    uint16_t depth;      /* Synthetic depth from isometric coordinates */
};

/* Single command buffer -- no double-buffering needed (see 4.8) */
static std::vector<DrawCommand> _draw_commands;
static int16_t _viewport_scroll_dx, _viewport_scroll_dy;
```

**Recording point:** `ViewportDrawParentSprites()` in `src/viewport.cpp` (line 1715), NOT `GfxBlitter()`. This is where we have both:
- Screen position (`ps->x`, `ps->y`, `ps->left`, `ps->top`)
- World-space coordinates (`ps->xmin`, `ps->ymin`, `ps->zmin`) for synthetic depth

Recording at this level gives us accurate depth data without threading tile coordinates through the entire sprite draw pipeline. `GfxBlitter()` only has screen coordinates and a sprite pointer -- no world-space data.

We add ~30 lines to `ViewportDrawParentSprites()` to record a `DrawCommand` per parent sprite. The blitter itself is never touched.

### 4.3 Motion Vector Sources

| Source | How Recorded | Accuracy |
|--------|-------------|----------|
| **Viewport scroll** | `prev_virtual_pos - curr_virtual_pos`, scaled by zoom level | Exact |
| **Vehicle motion** | Cache previous screen position per vehicle; delta = current - previous | Good for smooth motion |
| **Static tiles** | Default to viewport scroll motion (correct during scroll, zero when static) | Exact |
| **Sprite animation** | Same position, different sprite frame -- record (0,0) motion | Imperfect but acceptable |

**Viewport scroll motion formula (corrected for zoom):**
```cpp
int zoom_shift = to_underlying(vp->zoom) - to_underlying(ZoomLevel::Normal);
int16_t global_dx = (prev_scroll_x - curr_scroll_x) >> zoom_shift;
int16_t global_dy = (prev_scroll_y - curr_scroll_y) >> zoom_shift;
```

### 4.4 GPU-Side Motion Vector Rasterization (Tile-Based)

A naive O(pixels * commands) brute-force loop (5000 commands * 2M pixels = 10B iterations) would cause severe GPU thread divergence on older hardware like the GTX 1060. Instead, we use **tile-based spatial binning** from the start.

**Step 1 (CPU): Build per-tile command lists**

Divide the screen into 16x16 pixel tiles. For each draw command, append it to every tile it overlaps. Most tiles overlap <10 commands.

```cpp
/* CPU-side tile binning (~0.1ms for 5000 commands at 1080p) */
constexpr int TILE_SIZE = 16;
int tiles_x = (screen_w + TILE_SIZE - 1) / TILE_SIZE;  /* 120 at 1080p */
int tiles_y = (screen_h + TILE_SIZE - 1) / TILE_SIZE;   /* 68 at 1080p */

struct TileBin {
    uint16_t cmd_indices[MAX_CMDS_PER_TILE]; /* indices into draw command buffer */
    uint16_t count;
};

/* For each DrawCommand, insert into overlapping tiles */
for (uint16_t i = 0; i < commands.size(); i++) {
    auto &cmd = commands[i];
    int tx0 = cmd.screen_rect.left / TILE_SIZE;
    int ty0 = cmd.screen_rect.top / TILE_SIZE;
    int tx1 = (cmd.screen_rect.left + cmd.screen_rect.width - 1) / TILE_SIZE;
    int ty1 = (cmd.screen_rect.top + cmd.screen_rect.height - 1) / TILE_SIZE;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            tile_bins[ty * tiles_x + tx].cmd_indices[count++] = i;
}
```

**Step 2 (GPU): Per-tile MV rasterization**

Each workgroup processes one 16x16 tile, iterating only the commands that overlap that tile.

```glsl
#version 430

struct DrawCommand {
    ivec4 screen_rect;  /* x, y, w, h */
    ivec2 motion;       /* dx, dy in 1/8 pixel */
    uint depth;
    uint padding;
};

layout(std430, binding = 0) readonly buffer Commands { DrawCommand commands[]; };
layout(std430, binding = 1) readonly buffer TileBins {
    uint tile_counts[];  /* per-tile command count */
    /* followed by per-tile command index arrays */
};

layout(rg16_snorm, binding = 0) uniform image2D mv_texture;
layout(r16f, binding = 1) uniform image2D depth_texture;

uniform ivec2 screen_size;
uniform ivec2 viewport_motion;

layout(local_size_x = 16, local_size_y = 16) in;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= screen_size.x || pixel.y >= screen_size.y) return;

    uint tile_idx = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
    uint cmd_count = tile_counts[tile_idx];

    ivec2 best_motion = viewport_motion;
    float best_depth = 0.0;

    /* Only iterate commands overlapping THIS tile (typically <10) */
    for (uint i = 0; i < cmd_count; i++) {
        uint cmd_idx = /* fetch from tile bin */;
        DrawCommand cmd = commands[cmd_idx];
        if (pixel.x >= cmd.screen_rect.x &&
            pixel.x < cmd.screen_rect.x + cmd.screen_rect.z &&
            pixel.y >= cmd.screen_rect.y &&
            pixel.y < cmd.screen_rect.y + cmd.screen_rect.w) {
            float d = uintBitsToFloat(cmd.depth);
            if (d >= best_depth) {
                best_motion = cmd.motion;
                best_depth = d;
            }
        }
    }

    vec2 mv_normalized = vec2(best_motion) / 8.0 / vec2(screen_size);
    imageStore(mv_texture, pixel, vec4(mv_normalized, 0, 0));
    imageStore(depth_texture, pixel, vec4(best_depth));
}
```

**Performance:** O(pixels * commands_per_tile) with ~5-10 commands per tile typically. Well within the <1ms budget on GTX 1060.

### 4.5 Synthetic Depth Buffer

Depth derived from isometric world coordinates available in `ParentSpriteToDraw`:

```cpp
/* In ViewportDrawParentSprites, when recording DrawCommand */
float depth = (ps->xmin + ps->ymin + ps->zmin * 2.0f) / max_world_diagonal;
```

These world coordinates (`xmin`, `ymin`, `zmin`) are already computed during viewport sprite collection (`AddSortableSpriteToDraw` at viewport.cpp:715-732). For GUI elements drawn outside the viewport, depth = 1.0 (foreground).

### 4.6 Sub-Pixel Jitter Strategy

**Problem:** Pixel art has hard edges at integer pixel boundaries. Standard Halton sequence jitter causes temporal shimmer.

**Solution for prototype:**
- Jitter applied ONLY when `render_scale < 100%` AND zoom level is `Out2x` or further
- At `Normal` or `In2x`/`In4x` zoom, jitter disabled (pixel art must be pixel-perfect)
- Jitter amplitude reduced to 0.25 pixels (half of standard FSR 2 jitter)
- If shimmer is still unacceptable, jitter disabled entirely and FSR 2 operates in "no-jitter" degraded mode

### 4.7 Platform Strategy for Phase 2a

| Platform | Compute Path | FSR 2 Path |
|----------|-------------|------------|
| Windows (GL 4.3+) | OpenGL compute shaders | FSR 2 OpenGL port |
| Windows (GL 3.2-4.2) | Fallback to FSR 1 | N/A |
| Linux (GL 4.3+) | OpenGL compute shaders | FSR 2 OpenGL port |
| **macOS** | **Not supported in Phase 2a** | Fallback to FSR 1 |

macOS caps OpenGL at 4.1 (Apple deprecated OpenGL, never shipped 4.3+). macOS compute support requires either:
- SDL_GPU (SDL3) with Metal backend -- evaluated in Phase 3
- Direct Metal compute backend -- out of scope for prototype

### 4.8 Threading Safety

**Correction from v2.0 initial draft:** The draw-command recording and consumption both happen on the **same thread** (the main/draw thread). The actual call chain is:

```
Main/draw thread:
  VideoDriver::Tick()                          (video_driver.cpp:104)
    -> lock game_state_mutex                   (video_driver.cpp:129)
    -> UpdateWindows()                         (video_driver.cpp:153)
      -> DrawDirtyBlocks()
        -> Window::DrawViewport()
          -> ViewportDoDraw()
            -> ViewportDrawParentSprites()     <-- records draw commands
              -> DrawSpriteViewport()
                -> GfxBlitter()                <-- blitter runs here
    -> PopulateSystemSprites()
    -> unlock game_state_mutex
    -> Paint()                                 <-- consumes draw commands
```

Both the producer (`ViewportDrawParentSprites` recording commands) and consumer (`Paint()` uploading to GPU) run on the main/draw thread within the same `Tick()` call. The game thread is blocked by `game_state_mutex` during this entire sequence.

**This simplifies the design considerably.** No double-buffering, no swap synchronization, no fence sync for the command buffer:

```cpp
/* Single command buffer -- cleared at start of draw tick, consumed in Paint() */
static std::vector<DrawCommand> _draw_commands;

/* In UpdateWindows() or ViewportDoDraw(), before drawing: */
_draw_commands.clear();

/* In ViewportDrawParentSprites(), during drawing: */
_draw_commands.push_back({screen_rect, dx, dy, depth});

/* In Paint(), after rendering framebuffer quad: */
UploadDrawCommandsToGPU(_draw_commands);
DispatchMVRasterization();
```

### 4.9 Prototype Scope (4 weeks)

- [ ] Draw-command recording in GfxBlitter (~50 lines)
- [ ] GPU MV rasterization compute shader (~100 lines)
- [ ] Synthetic depth from tile coordinates (~30 lines)
- [ ] FSR 2 OpenGL integration (vendor ~1200 lines, glue ~200 lines)
- [ ] Jitter system with zoom-aware amplitude (~50 lines)
- [ ] A/B comparison tool: FSR 1 vs FSR 2 side-by-side screenshot
- [ ] Windows-only (fastest iteration path)

### 4.10 Quality Gate 2 (HARD KILL)

**This gate determines whether temporal upscaling has any future in OpenTTD.**

Test protocol:
1. Render 10 representative scenarios at 67% scale:
   - Static city view (no scroll, no vehicle motion)
   - Fast viewport scroll across landscape
   - Dense train network with moving vehicles
   - Palette-animated water/coastline
   - Building placement with ghost sprites
2. Capture FSR 1 and FSR 2 outputs for each scenario
3. Blind A/B test with 20+ participants
4. **FSR 2 must be preferred over FSR 1 in at least 7/10 scenarios by at least 60% of participants**

**If Gate 2 fails:** Stop. FSR 1 spatial upscaling is the ceiling. Phase 2b and Phase 3 are cancelled. Document findings for future revisiting when temporal upscaling technology evolves.

---

## 5. Phase 2b: Full FSR 2 Integration

**Duration:** 8-12 weeks
**Risk:** Medium
**Status:** GATED on Quality Gate 2 passing

### 5.1 Objectives (only if Gate 2 passes)

- Production-quality FSR 2 across Windows and Linux
- macOS strategy: evaluate SDL_GPU (SDL3) Metal compute path
- Robust fallback chain: FSR 2 -> FSR 1 -> CAS -> None
- Settings UI with quality mode selection
- Edge case handling (fast scroll history reset, 8bpp fallback)

### 5.2 Edge Case Handling

| Edge Case | Mitigation |
|-----------|-----------|
| Fast viewport scroll (>32px/frame) | Reset FSR 2 history buffer, degrade to spatial for 1 frame |
| Sprite animation frame changes | Record (0,0) motion; accept minor temporal artifacts |
| Palette animation (water, fire) | Reset FSR 2 history on palette change (treat as scene cut). Crude but avoids reactive mask scan. If palette changes are too frequent (water always visible), FSR 2 quality may degrade to FSR 1 level -- Gate 2 will catch this. |
| 8bpp blitter active | Auto-fallback to FSR 1 (no compute path for palette-indexed buffers) |
| Multiple viewports | Generate separate MV command lists per viewport |
| Zoom level change | Reset FSR 2 history buffer |
| Window resize | Reset FSR 2 history buffer, reallocate FBOs |

### 5.3 FSR 2 NVIDIA Performance Mitigation

The FSR 2 OpenGL compute path is documented as 3x slower on NVIDIA GPUs. Mitigations:

1. **Default to FSR 1 on NVIDIA GPUs** until Phase 3 (Vulkan) is available
2. If user manually enables FSR 2 on NVIDIA OpenGL, display performance warning
3. Benchmark FSR 2 GPU time; if >4ms at target resolution, auto-downgrade to FSR 1

### 5.4 Estimated LOC

- Draw-command recording: ~100 lines
- GPU MV rasterization (compute): ~200 lines
- Depth generation: ~80 lines
- Jitter system: ~80 lines
- FSR 2 integration glue: ~400 lines
- FSR 2 OpenGL backend (vendored, MIT): ~1,200 lines
- Edge case handlers: ~200 lines
- Settings/UI: ~100 lines
- **Total: ~2,360 lines** (excluding vendored FSR 2 SDK)

---

## 6. Phase 3: Vulkan/DX11 Backend + DLSS

**Duration:** 16-30 weeks (revised from 12-20)
**Risk:** High
**Status:** GATED on Quality Gate 3

### 6.1 Quality Gate 3 (All must pass)

- [ ] Phase 2b FSR 2 delivers measurable quality improvement over FSR 1
- [ ] Community polling shows demand for DLSS integration (>30% positive)
- [ ] GPLv2 plugin architecture validated with legal review
- [ ] Maintainer commitment for long-term backend support

### 6.2 Architecture: Composition Over Inheritance

**Do NOT create new top-level video drivers.** Instead, the existing drivers gain the ability to use alternative render backends internally:

```cpp
/* Render backend abstraction (composed inside existing video drivers) */
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    /* Lifecycle */
    virtual bool Init(const Dimension &screen_res) = 0;
    virtual void Shutdown() = 0;
    virtual bool Resize(int w, int h, bool force = false) = 0;

    /* CPU buffer access (for blitter) */
    virtual void *MapVideoBuffer() = 0;
    virtual void UnmapVideoBuffer(const Rect &dirty) = 0;

    /* Animation buffer (for 40bpp blitter) */
    virtual bool HasAnimBuffer() const { return false; }
    virtual uint8_t *MapAnimBuffer() { return nullptr; }
    virtual void UnmapAnimBuffer(const Rect &dirty) {}

    /* Rendering */
    virtual void Paint() = 0;
    virtual void UpdatePalette(const Colour *pal, uint first, uint length) = 0;

    /* Cursor (hardware cursor support) */
    virtual void DrawMouseCursor() {}
    virtual void PopulateCursorCache() {}
    virtual void ClearCursorCache() {}

    /* Post-processing */
    virtual void SetUpscaleMode(UpscaleMode mode) = 0;
    virtual void SetRenderScale(int percent) = 0;
    virtual bool SupportsTemporalUpscaling() const { return false; }

    /* Motion vector input */
    virtual void SubmitDrawCommands(std::span<const DrawCommand> cmds,
                                     int16_t scroll_dx, int16_t scroll_dy) {}

    /* Upscaling plugin (for DLSS) */
    virtual void LoadUpscalePlugin(const char *path) {}

    /* Info */
    virtual std::string GetDriverName() const = 0;
    virtual bool SupportsCompute() const { return false; }
};
```

The existing `VideoDriver_Win32OpenGL` / `VideoDriver_SDL_OpenGL` would hold a `std::unique_ptr<RenderBackend>`, selecting at runtime:

```cpp
/* Inside VideoDriver_Win32OpenGL::AllocateContext() */
if (VulkanRenderBackend::IsAvailable()) {
    this->backend = std::make_unique<VulkanRenderBackend>();
} else {
    this->backend = std::make_unique<OpenGLRenderBackend>();  /* existing */
}
```

**Driver count remains at 9. No new driver registrations. No factory changes.**

### 6.3 DLSS as Optional Plugin (GPLv2 Compliance)

DLSS integration **must** use a plugin architecture with a stable C ABI boundary:

```cpp
/* dlss_plugin_api.h -- this file is MIT-licensed, shipped with OpenTTD */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DLSSPluginAPI {
    int  (*init)(void *d3d11_device, int render_w, int render_h, int display_w, int display_h);
    void (*shutdown)(void);
    int  (*evaluate)(void *color_tex, void *mv_tex, void *depth_tex,
                     void *output_tex, float jitter_x, float jitter_y);
    void (*set_mode)(int quality_mode);  /* 0=Quality, 1=Balanced, 2=Performance */
    const char *(*get_version)(void);
} DLSSPluginAPI;

/* Loaded at runtime via LoadLibrary/dlopen */
typedef DLSSPluginAPI *(*GetDLSSPluginFunc)(void);

#ifdef __cplusplus
}
#endif
```

The plugin DLL (containing Streamline SDK + DLSS DLLs) is:
- Distributed separately from the GPLv2 OpenTTD binary
- Optional -- game works without it
- Loaded via `LoadLibrary()` / `dlopen()` at runtime
- Communicates only through the C ABI defined above
- Never `#include`d by the main codebase

### 6.4 Evaluate SDL_GPU Before Hand-Writing Backends

**Before committing to hand-written Vulkan + DX11 backends**, evaluate SDL_GPU (available in SDL3):

| Factor | Hand-Written Vulkan + DX11 | SDL_GPU (SDL3) |
|--------|---------------------------|----------------|
| LOC | 5,000-8,000 (realistic) | ~1,500-2,500 |
| Platforms | Vulkan (Win/Linux), DX11 (Win) | Vulkan, DX12, Metal (all platforms) |
| Maintenance | High (two APIs to maintain) | Low (one abstraction) |
| macOS | Requires MoltenVK | Native Metal via SDL_GPU |
| Compute shaders | Full control | Supported in SDL_GPU |
| DLSS integration | Direct DX11 access | May need DX11 interop layer |
| Risk | High complexity | SDL3 migration risk |

**Recommendation:** Prototype Phase 3 on SDL_GPU first. If SDL_GPU's compute shader support is sufficient for FSR 3 dispatch, use it as the sole new backend. Only fall back to hand-written Vulkan/DX11 if SDL_GPU proves inadequate.

### 6.5 Estimated LOC (Revised)

| Component | SDL_GPU Path | Hand-Written Path |
|-----------|-------------|-------------------|
| RenderBackend abstraction | 200 | 200 |
| New backend implementation | 2,000 | 5,000-8,000 |
| DLSS plugin interface | 200 | 200 |
| DLSS plugin implementation | 500 | 500 |
| FSR 3 integration | 400 | 400 |
| Build system changes | 150 | 300 |
| **Total** | **~3,450** | **~6,600-9,600** |

---

## 7. Hardware Compatibility Matrix (Revised)

| GPU | Phase 1 | Phase 2 | Phase 3 |
|-----|---------|---------|---------|
| Software (no GL) | No upscaling | No | No |
| Intel/AMD GL 3.2-4.2 | FSR 1 + CAS | FSR 1 (no compute) | N/A |
| Any GPU GL 4.3+ | FSR 1 + CAS | FSR 2 | N/A |
| macOS (GL 4.1 max) | FSR 1 + CAS | FSR 1 (pending Metal/SDL_GPU) | FSR 2/3 via SDL_GPU Metal |
| AMD RDNA+ (Vulkan) | FSR 1 + CAS | FSR 2 | FSR 3 |
| NVIDIA GTX (GL 4.3+) | FSR 1 + CAS | FSR 1 default (FSR 2 opt-in, slow) | FSR 3 via Vulkan |
| NVIDIA RTX 20/30/40/50 | FSR 1 + CAS | FSR 1 default (FSR 2 opt-in) | DLSS SR via plugin + FSR 3 |

**Principle: Every phase is additive. No existing functionality is removed. All upscaling defaults to OFF.**

---

## 8. Fallback Chain (Revised)

Auto-selection based on capabilities (user can override):

```
1. DLSS Super Resolution  (RTX GPU + Phase 3 backend + plugin installed)
2. FSR 3 Upscaling        (Phase 3 backend, any GPU with compute)
3. FSR 2 Upscaling        (GL 4.3+ compute, any GPU -- not NVIDIA default)
4. FSR 1 Spatial           (GL 3.2+ fragment shader, any GPU)
5. CAS Sharpening only     (any OpenGL GPU)
6. No upscaling            (native resolution, any backend)
```

On battery power: auto-downgrade to FSR 1 or disabled.

---

## 9. Performance Budgets (New)

| Budget | Target | Measured On |
|--------|--------|-------------|
| Draw-command recording | < 0.2ms @ 1080p | i5-10400 |
| MV GPU rasterization | < 1ms @ 1080p | GTX 1060 6GB |
| PBO upload (color only) | < 2ms @ 1080p, < 4ms @ 4K | PCIe 3.0 x16 |
| FSR 1 GPU cost | < 0.5ms @ 1080p | Any GL 3.2 GPU |
| FSR 2 GPU cost | < 4ms @ 1080p | GTX 1060 / RX 580 |
| End-to-end latency | < 50ms | Any configuration |
| Total upscaling pipeline | < 6ms @ 1080p | Including all phases |
| SSE blitter regression | 0% (blitters untouched) | N/A |
| GPU power draw increase | < 15W above idle | Mobile GPU |
| Phase 1 overhead at 100% | < 0.5ms | Any GPU (no-op pass) |

### Required Benchmarks

1. **PBO upload throughput** at 1080p, 1440p, 4K
2. **FSR 2 GPU dispatch time** on NVIDIA (OpenGL), AMD (OpenGL), Intel Arc
3. **Draw-command recording CPU cost** with perf counters
4. **Frame pacing histogram** (10K frames, each upscale mode)
5. **Battery life comparison** (30 min session, upscaling OFF vs FSR 1 vs FSR 2)

---

## 10. Risk Register (Revised)

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|------------|--------|------------|
| R1 | Draw-command MV inaccurate for sprite animation | High | Medium | Accept: sprite animation MVs are (0,0); FSR 2 handles via temporal accumulation. Gate 2 tests this. |
| R2 | FSR 2 on OpenGL slow on NVIDIA | Confirmed | High | Default to FSR 1 on NVIDIA until Vulkan backend available. |
| R3 | Sub-pixel jitter shimmer on pixel art | High | Medium | Jitter only at sub-native scale + zoomed-out. Gate 2 tests this. |
| R4 | macOS has no compute path in Phase 2 | Confirmed | Medium | macOS stays on FSR 1 until Phase 3 (SDL_GPU/Metal). Explicitly documented. |
| R5 | GPLv2 + DLSS licensing | Confirmed blocker | Critical | Plugin architecture with C ABI. Validated before Phase 3 starts. |
| R6 | Temporal upscaling provides no benefit for 2D pixel art | Medium | Critical | Gate 2 is a hard kill. If it fails, project stops at Phase 1. |
| R7 | SDL_GPU (SDL3) not mature enough | Medium | High | Evaluate early in Phase 3. Fall back to hand-written backends if needed. |
| R8 | Community rejects GPU features | Medium | Medium | All features off by default. Zero impact on existing experience. |
| R9 | Multiple viewports with different MVs | Low | Medium | Separate command lists per viewport. Tested in Phase 2b. |
| R10 | Maintenance burden unsustainable for volunteers | High | Critical | Drop DX11 (use Vulkan on Windows). Gate Phase 3 on maintainer commitment. 12-month deprecation policy for unmaintained backends. |
| R11 | NewGRF sprite artifacts from post-processing | Medium | Medium | "Pixel-perfect mode" bypasses all PP at 100% scale. Involve NewGRF creators in Phase 2 testing. |
| R12 | FSR 4 licensing shift to proprietary DLLs | Medium | High | Keep FSR 2/3 open-source as primary path. If FSR 4 needs signed DLLs, apply same plugin architecture as DLSS. |
| R13 | Build complexity blocks contributors | Medium | High | All backends compile-time optional. Default build requires zero new SDKs. Pre-compile SPIR-V shaders. |

---

## 11. Build System Requirements (New)

All GPU backends are **compile-time optional**. The default build succeeds with zero new SDK dependencies.

```cmake
option(WITH_FSR1 "Enable FSR 1 spatial upscaling (OpenGL)" ON)    # Phase 1, always ON
option(WITH_FSR2 "Enable FSR 2 temporal upscaling (OpenGL)" OFF)   # Phase 2
option(WITH_VULKAN "Enable Vulkan render backend" OFF)              # Phase 3
option(WITH_FSR3 "Enable FSR 3 upscaling (Vulkan)" OFF)            # Phase 3
# DLSS is never a build option -- it is a runtime plugin loaded via dlopen/LoadLibrary
```

SPIR-V shaders are pre-compiled and checked into the repository so contributors do not need the SPIR-V toolchain.

---

## 12. User-Facing Settings (New)

**Simplified UI (not raw technology names):**

```
Upscaling: [Off] [Performance] [Balanced] [Quality]
Render scale: [50%---|---75%---|---100%]
Sharpening: [Off] [Low] [Medium] [High]

Status: "Using FSR 2 (OpenGL compute) at 75% render scale"
```

Auto-selection logic (hidden from user):
- **Quality**: DLSS SR (if plugin) > FSR 3 (if Vulkan) > FSR 2 (if GL 4.3) > FSR 1
- **Balanced**: FSR 2 > FSR 1 (skip DLSS overhead if not needed)
- **Performance**: FSR 1 (lightest GPU cost)

CAS sharpening is always applied when any upscaling is active. Not exposed as a separate option.

At 100% render scale, "pixel-perfect mode" -- all post-processing bypassed.

---

## 13. Platform Coverage (New)

| Platform | Phase 1 | Phase 2 | Phase 3 |
|----------|---------|---------|---------|
| Windows (GL 4.3+) | FSR 1 + CAS | FSR 2 | DLSS (plugin) + FSR 3 (Vulkan) |
| Windows (GL 3.2) | FSR 1 + CAS | FSR 1 fallback | FSR 3 (Vulkan) |
| Linux (GL 4.3+) | FSR 1 + CAS | FSR 2 | FSR 3 (Vulkan) |
| Linux (GL 3.2) | FSR 1 + CAS | FSR 1 fallback | FSR 3 (Vulkan) |
| macOS | FSR 1 + CAS | **FSR 1 only** (GL 4.1 max) | FSR 2/3 via SDL_GPU Metal |
| FreeBSD | FSR 1 + CAS | FSR 2 (if GL 4.3) | Vulkan (mesa) |
| Emscripten/Web | No (WebGL limits) | No | Future: WebGPU |
| Software/GDI | No change | No change | No change |

All platforms retain existing functionality with zero regression.

---

## 14. Open Questions

1. **SDL3 migration timeline:** OpenTTD uses SDL2. Phase 3's SDL_GPU requires SDL3. Is the project planning this independently?

2. **Vehicle screen position caching across multiple viewports:** A vehicle may appear in N viewports at different zooms. Draw-command approach handles naturally (per-viewport command lists), but vehicle "previous position" must be tracked per-viewport.

3. **NewGRF sprite alignment under jitter:** Pixel-perfect tile alignment may break under sub-pixel jitter. Testing needed in Phase 2a.

4. **macOS long-term strategy:** MoltenVK (community Vulkan-on-Metal) vs native Metal backend. Metal would eliminate the translation layer but adds an Apple-specific rendering path.

5. **Maintenance commitment:** Who maintains GPU backends long-term? Phase 3 should not proceed without an identified maintainer willing to track SDK updates.

6. **Palette animation frequency:** `PaletteAnimate()` (32bpp_anim.cpp:487) directly modifies the framebuffer outside the sprite draw pipeline, bypassing draw-command recording entirely. If palette animation triggers every frame (water always visible), the FSR 2 history-reset approach degrades quality to FSR 1 level. A more sophisticated reactive mask approach may be needed, but adds O(width*height) scan cost. Gate 2 testing will determine if this is acceptable.

7. **Frame interpolation synergy:** The draw-command MV infrastructure from Phase 2 captures per-sprite motion data that could also serve **frame interpolation** between game ticks. At 240Hz displays with 33fps game state updates, ~6.5 frames show identical content. The MV data could interpolate sprite positions for smooth inter-tick rendering. This makes the Phase 2 investment more valuable beyond upscaling alone, and should be explored as a future Phase 2c.

8. **Redundant frame skipping:** When the game state hasn't changed and the viewport hasn't scrolled, the framebuffer is pixel-identical to the previous frame. Tracking a visual generation counter and skipping Paint() (including the upscale shader) when nothing changed is a cheap independent optimization. Consider implementing this as a "Phase 0" before Phase 1, as it directly reduces GPU cost at high refresh rates.

---

## 15. Summary: What We Commit To

| What | Status | Depends On |
|------|--------|-----------|
| Phase 1: FBO + FSR 1 + CAS | **Committed** | Nothing |
| Phase 2a: MV prototype + FSR 2 POC | **Committed** | Phase 1 |
| Phase 2b: Production FSR 2 | **Gated** | Gate 2 pass |
| Phase 3: Vulkan/DX + DLSS | **Gated** | Gate 3 pass |

The minimum viable outcome is Phase 1: spatial upscaling and sharpening in ~540 lines of code. Everything beyond that is gated on empirical evidence that temporal upscaling improves quality for 2D pixel art content.

Sources:
- [NVIDIA DLSS 4.5 Technical Blog](https://developer.nvidia.com/blog/nvidia-dlss-4-5-delivers-super-resolution-upgrades-and-new-dynamic-multi-frame-generation/)
- [DLSS 4.5 on all RTX GPUs](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-super-resolution-available-now/)
- [Streamline SDK GitHub](https://github.com/NVIDIA-RTX/Streamline)
- [Streamline DLSS Programming Guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md)
- [FSR 2 OpenGL Port Blog](https://juandiegomontoya.github.io/porting_fsr2.html)
- [FSR 2 OpenGL GitHub](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL)
- [FSR 2 GPUOpen](https://gpuopen.com/fidelityfx-superresolution-2/)
- [FSR 3 GPUOpen](https://gpuopen.com/fidelityfx-super-resolution-3/)
