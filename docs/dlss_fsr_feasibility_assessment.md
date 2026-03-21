# DLSS 5 & FSR Integration Feasibility Assessment

**Date:** 2026-03-21
**Conclusion:** Native DLSS 5 / FSR 4 integration is **not feasible** without a fundamental rewrite of the rendering backend. However, several alternative paths exist that range from practical (shader-based post-processing) to ambitious (new Vulkan/DX12 rendering backend).

---

## 1. Executive Summary

OpenTTD's rendering pipeline is entirely CPU-based. The GPU (via OpenGL) is used only as a dumb framebuffer display. DLSS 5 and FSR 4 both require DirectX 11/12 or Vulkan -- APIs that OpenTTD does not use for rendering. Additionally, both technologies require per-pixel motion vectors that OpenTTD does not generate.

### Verdict by Technology

| Technology | Feasibility | Effort | Benefit for OpenTTD |
|------------|-------------|--------|---------------------|
| DLSS 5 (Neural Rendering) | Not feasible | Would require new DX/Vulkan backend + motion vector generation | Low -- designed for photorealism, wrong for pixel art/isometric |
| FSR 4 (ML Upscaling) | Not feasible | Requires DX12 backend + depth + motion vectors, RX 9000 only | Very low -- vendor-locked, wrong use case |
| FSR 1 (Spatial Upscaling) | **Feasible** | Moderate -- GLSL shader in existing OpenGL path | Moderate -- simple upscaling with sharpening |
| NIS (NVIDIA Image Scaling) | **Feasible** | Low -- GLSL shader in existing OpenGL path | Moderate -- spatial upscaler + sharpening |
| Custom post-process shaders | **Feasible** | Moderate -- extend OpenGL backend | High -- CRT filters, pixel art upscaling, stylization |
| Vulkan backend (long-term) | Possible | **Massive** -- new video driver, rewrite rendering | Enables future DLSS/FSR integration |

## 2. Blocking Gaps Analysis

### 2.1 Gap: No DirectX or Vulkan Rendering Path

**Current state:** OpenTTD uses OpenGL (for presentation) or pure software (GDI/SDL surface). There is no DirectX or Vulkan code anywhere in the codebase.

**Why it matters:**
- DLSS 5 requires DirectX 11, DirectX 12, or Vulkan via the Streamline SDK
- FSR 4 requires DirectX 12 exclusively
- The Streamline SDK does not support OpenGL at all

**What would be needed:** A completely new video driver backend implementing either Vulkan or DirectX 12, including:
- Device/instance creation and management
- Swap chain management
- Command buffer recording
- Texture/buffer resource management
- Shader pipeline (compute and graphics)
- Synchronization primitives

**Estimated effort:** 3,000-5,000+ lines of new code, significant ongoing maintenance burden for a new rendering API. This alone is a multi-month project.

### 2.2 Gap: No Motion Vector Generation

**Current state:** OpenTTD has no concept of per-pixel motion vectors. The game renders each frame independently as a full-screen composite of sprites onto a CPU buffer.

**Why it matters:**
- DLSS 5 requires motion vectors as input (the only input besides color)
- FSR 4 requires motion vectors
- All temporal upscaling/enhancement technologies need frame-to-frame motion data

**Why this is especially hard for OpenTTD:**
- The game is sprite-based, not geometry-based -- there are no 3D transforms to derive motion from
- Viewport scrolling is the primary source of motion, which could theoretically be computed from scroll delta
- Individual sprite motion (vehicles, smoke, water animation) would need per-pixel tracking
- The CPU-based blitter has no concept of "which sprite contributed to this pixel"

**Potential approaches (all non-trivial):**
1. **Viewport-only motion vectors:** Generate uniform motion vectors from viewport scroll delta. Cheap but misses object motion entirely.
2. **Sprite-tracked motion vectors:** Track which sprite/object occupies each pixel and compute motion from game state. Requires a fundamental change to the blitter to write object IDs alongside colour.
3. **Optical flow estimation:** Use GPU compute to estimate motion vectors from consecutive frames. Expensive, approximate, adds latency.

### 2.3 Gap: No Depth Buffer

**Current state:** No depth information exists. The game uses painter's algorithm (back-to-front) for compositing, with draw order determined by the tile loop and viewport system.

**Why it matters:** FSR 4 requires a depth buffer. DLSS 5 does not (as of current documentation).

**Potential approach:** Generate a synthetic depth buffer from the isometric tile coordinates. Each tile has implicit Z-order based on its (x, y) map position and height. This is achievable but requires the blitter to write depth values alongside colour.

### 2.4 Gap: No Post-Processing Pipeline

**Current state:** The OpenGL backend draws exactly one fullscreen quad with the framebuffer texture. There are no framebuffer objects (FBOs), no off-screen render targets, no multi-pass rendering.

**Why it matters:** Any image enhancement (whether DLSS, FSR, or custom shaders) requires at least:
1. Rendering to an off-screen texture (not directly to screen)
2. Applying one or more post-processing passes
3. Presenting the final result

**What would be needed:** Extend the OpenGL backend with:
- FBO creation and management
- Render-to-texture capability
- A post-processing pass system
- Additional shader programs for enhancement

**Estimated effort:** 200-500 lines of code. This is the most tractable gap to close.

## 3. DLSS 5 Specific Assessment

### 3.1 Could DLSS 5 Work for OpenTTD's Visual Style?

**No, and for fundamental reasons:**

1. **Photorealism bias:** DLSS 5's neural rendering model is trained on photorealistic game content. It infers subsurface scattering, fabric sheen, and physically-based material properties. OpenTTD uses low-resolution pixel art sprites with palette-based coloring. The AI would attempt to "enhance" pixel art with photorealistic lighting, which would destroy the art style rather than improve it.

2. **Art style mismatch:** DLSS 5 has already faced criticism for overriding artistic intent in stylized 3D games. For a 2D pixel art game, the mismatch would be dramatically worse. The neural network has no training data for isometric pixel art enhancement.

3. **Resolution model mismatch:** DLSS 5 is designed to work with high-resolution 3D rendered frames (1080p+). OpenTTD's internal rendering resolution with pixel art sprites would likely confuse the model.

4. **Developer controls would not help:** While DLSS 5 offers intensity/masking/color grading controls, these are designed for tuning photorealistic enhancement, not for adapting the system to a completely different visual paradigm.

### 3.2 Motion Vector Feasibility

Even if the API and art style problems were solved, generating useful motion vectors for DLSS 5 would be problematic:

- **Viewport scroll:** Easy to generate (uniform vector across the frame) but insufficient alone
- **Vehicle motion:** Would need per-vehicle, per-pixel tracking -- fundamentally at odds with the sprite blitting model
- **Animation cycles:** Sprite animation frames change discretely, not continuously -- temporal algorithms struggle with this
- **Palette animation:** Water, fire, and other palette-cycling effects have no spatial motion -- they change colour in place

## 4. FSR 4 Specific Assessment

### 4.1 Even Less Feasible Than DLSS 5

FSR 4 has all the same barriers as DLSS 5, plus additional constraints:
- **DX12 exclusive** -- narrower API requirement
- **RX 9000 exclusive** -- eliminates the vast majority of OpenTTD's player base
- **Requires depth buffer** -- additional rendering data OpenTTD doesn't generate
- **Designed for 3D content upscaling** -- same art style mismatch

## 5. Feasible Alternatives

### 5.1 Option A: OpenGL Post-Processing Pipeline (Recommended)

**What:** Extend the existing OpenGL backend with FBO-based post-processing.

**How it works:**
1. Render the CPU framebuffer to an off-screen texture (FBO) instead of directly to screen
2. Apply one or more shader passes to the texture
3. Present the final result to screen

**Possible shader effects:**
- **Spatial upscaling** (FSR 1 / NIS algorithm as GLSL shaders)
- **Sharpening** (CAS - Contrast Adaptive Sharpening)
- **CRT/retro filters** (scanlines, phosphor glow, curvature)
- **Pixel art upscaling** (xBR, HQx, ScaleHQ algorithms as shaders)
- **Color grading** (LUT-based colour transforms for art style)
- **Bloom/glow** (soft glow on bright elements)
- **Vignetting** (subtle darkening at screen edges)

**Effort:** Moderate (200-500 lines for the FBO infrastructure, plus per-effect shaders)
**Compatibility:** Works with existing OpenGL path on all platforms
**Performance impact:** Very low -- these are simple full-screen shader passes

### 5.2 Option B: FSR 1 Integration via OpenGL

**What:** Implement AMD's FSR 1 spatial upscaling as a GLSL post-processing shader.

**How it works:**
- FSR 1 is purely spatial (single-frame) -- no motion vectors or depth needed
- The algorithm is open-source and published as shader code
- It can be ported to GLSL for OpenGL
- Game renders at lower resolution, FSR 1 upscales to display resolution

**Benefit:** Better image quality when running at non-native resolutions
**Effort:** Low-moderate (FSR 1 shader port + FBO infrastructure from Option A)
**Limitation:** FSR 1 quality is modest compared to temporal solutions

### 5.3 Option C: Pixel Art Enhancement Shaders

**What:** Implement specialized pixel art upscaling algorithms designed for 2D content.

**Algorithms worth evaluating:**
- **xBR** (rule-based pixel art upscaler -- excellent for sprite edges)
- **HQ2x/HQ3x/HQ4x** (pattern-matching upscaler)
- **Anime4K** (ML-inspired but runs as shader -- good for 2D)
- **ScaleHQ** (high-quality pixel art filter)

These are specifically designed for the kind of content OpenTTD renders and would produce much better results than DLSS/FSR for this use case.

**Effort:** Moderate (shader implementations exist in open-source projects)
**Quality:** Potentially excellent for OpenTTD's art style

### 5.4 Option D: Art Style Transformation via Custom Shaders

**What:** Create custom GLSL shaders that transform the game's visual appearance into a specific art style.

**Examples:**
- **Tilt-shift miniature effect** (depth-of-field blur based on Y-position)
- **Watercolor/painterly filter** (edge detection + colour blending)
- **Blueprint/technical drawing** (edge extraction + monochrome)
- **Sepia/vintage** (colour grading + noise/grain)
- **Night mode** (colour temperature shift + luminance reduction)

These are achievable with the existing OpenGL backend and FBO infrastructure.

### 5.5 Option E: Vulkan/DX12 Backend (Long-Term)

**What:** Build a new rendering backend using Vulkan or DirectX 12.

**Why:**
- Enables future DLSS/FSR integration if those technologies evolve
- Better performance potential (lower driver overhead)
- Compute shader support for advanced effects
- Modern GPU feature access

**What it enables:**
- DLSS Super Resolution (temporal upscaling with motion vectors)
- FSR 2/3 (temporal upscaling, cross-vendor)
- Frame generation (DLSS 3 / FSR 3)
- Potentially DLSS 5 when it launches (Fall 2026)

**Effort:** Massive (months of work, significant complexity increase)
**Risk:** High -- OpenTTD's player base uses extremely diverse hardware, including old/low-end GPUs that may not support Vulkan/DX12 well

**Recommended only if** the project decides GPU-accelerated rendering is a strategic direction. The current CPU-based approach is actually well-suited to the game's needs and runs on nearly any hardware.

## 6. Recommended Path Forward

### Phase 1: Post-Processing Infrastructure (Low Risk, High Value)

Add FBO-based post-processing to the existing OpenGL backend:
- Create an off-screen FBO at render resolution
- Render the CPU framebuffer to this FBO
- Apply configurable post-processing shader passes
- Present to screen

This is a prerequisite for any visual enhancement and is valuable on its own.

### Phase 2: Spatial Upscaling & Sharpening

Implement FSR 1 or CAS (Contrast Adaptive Sharpening) as post-processing shaders. This gives an immediate visual quality improvement when running at non-native resolutions.

### Phase 3: Pixel Art Upscaling (Optional)

Evaluate xBR, HQ4x, and Anime4K upscaling shaders for pixel art. These could provide a "smooth" rendering mode that preserves the art style while reducing pixel staircase artifacts.

### Phase 4: Art Style Filters (Optional)

Add optional visual style filters (tilt-shift, color grading, CRT) that give users control over the game's visual presentation.

### Phase 5: Evaluate Vulkan Backend (Long-Term)

If Phases 1-4 prove popular and there is appetite for more advanced GPU features, evaluate building a Vulkan backend. This is the only path to native DLSS/FSR temporal upscaling integration.

## 7. Motion Vector Generation Strategy (If Needed)

If a Vulkan/DX12 backend is pursued and temporal upscaling is desired, here is how motion vectors could be generated for OpenTTD:

### 7.1 Viewport Motion (Easy)

The viewport scroll position (`vp->virtual_left`, `vp->virtual_top`) changes between frames. The delta directly gives a uniform motion vector for the background.

```
mv_x = (prev_scroll_x - curr_scroll_x) / screen_width
mv_y = (prev_scroll_y - curr_scroll_y) / screen_height
```

### 7.2 Vehicle Motion (Hard)

Each vehicle has a position (`Vehicle::x_pos`, `Vehicle::y_pos`). Between frames, the screen-space delta could be computed. However, mapping this to per-pixel motion vectors requires:
1. Knowing which pixels each vehicle sprite occupies
2. Writing motion vectors to those pixels during compositing
3. This fundamentally changes the blitter interface

### 7.3 Synthetic Approach (Moderate)

Use the viewport scroll vector as a base, then override with vehicle motion where detectable. Accept that palette animations and sprite frame changes will have incorrect motion vectors (causing temporal artifacts in those areas).

## 8. Conclusion

**DLSS 5 and FSR 4 are the wrong tools for OpenTTD.** They are designed for photorealistic 3D games and require rendering APIs and data that OpenTTD does not and should not generate.

**The right approach is shader-based post-processing** within the existing OpenGL backend. This provides:
- Immediate visual enhancement with minimal code changes
- Art style filters designed for 2D/pixel art content
- Cross-platform compatibility
- No vendor lock-in
- Low performance overhead

For users who want AI-based upscaling today, **external tools like Lossless Scaling or Magpie** already work with OpenTTD without any code changes.
