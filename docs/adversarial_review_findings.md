# Adversarial Review Findings: GPU Migration Plan v1.0

**Date:** 2026-03-21
**Input:** `gpu_migration_plan.md` (v1.0)
**Output:** `gpu_migration_plan_v2.md` (v2.0, hardened)

> **Status (2026-03-22):** All critical and high-severity findings from these reviews were addressed in v2.0, and all phases of the hardened plan have been implemented. The draw-command MV recording approach (replacing per-pixel blitter modification) proved sound. The plugin C ABI boundary for GPLv2/DLSS compliance is in place. See `gpu_migration_plan_v2.md` header for phase completion status.

Four independent hostile reviews were conducted. This document preserves all findings.

---

## Review 1: Technical Feasibility

### CRITICAL Findings

**F1.1: Blitter variant count drastically underestimated.**
The plan listed 2 blitters for MV modification. The codebase has 14 blitter classes with separate Draw() implementations, including 5 SSE-optimized variants. The SSE inner loops process 2 pixels at a time using 128-bit SIMD registers -- adding per-pixel MV writes breaks the SIMD pipeline. True LOC: 4,000-8,000+, not "~200 per variant."

**F1.2: macOS completely broken.**
Apple deprecated OpenGL and never shipped support beyond OpenGL 4.1. FSR 2 requires OpenGL 4.3+ compute shaders. Phase 2 is impossible on macOS. The plan never mentioned macOS.

**F1.3: Threading model creates race conditions.**
Blitter Draw() runs on the game thread. OpenGL functions run on the main thread. The MV buffer would need double-buffering with fence sync, identical to the existing PBO pattern. The plan mentioned no synchronization.

### HIGH Findings

**F1.4:** SDL2 OpenGL driver does not request GL 4.3+. Linux support for compute shaders uncertain.

**F1.5:** FSR 2 on OpenGL 3x slower on NVIDIA (80% of discrete GPU market). Phase 2 delivers inferior results on the dominant hardware.

**F1.6:** Motion vector generation architecturally flawed: no frame-to-frame history in blitter, multi-viewport makes per-vehicle caching ambiguous, sprite animation creates discontinuous motion.

**F1.7:** Sub-pixel jitter and pixel art are fundamentally incompatible. FSR 2 can never improve native-resolution pixel art.

**F1.8:** Vulkan backend LOC estimate of 2,500 is 2-3x too low. Minimum realistic: 5,000-8,000.

### MEDIUM Findings

**F1.9:** DX11 + Streamline integration model unvalidated for "CPU upload then enhance" workflow.

**F1.10:** 8bpp blitter path entirely ignored.

**F1.11:** Viewport scroll MV formula ignores zoom level scaling.

**F1.12:** Time estimates 2-4x too optimistic across all phases.

---

## Review 2: Architecture

### CRITICAL Findings

**F2.1: Phase coupling / missing kill criteria.**
No quality gate between phases. If motion vectors don't work for 2D (which is genuinely uncertain), Phase 3's entire value proposition collapses. 12-20 weeks wasted.

**F2.2: GPLv2 + proprietary SDK licensing.**
DLSS requires proprietary NVIDIA DLLs. GPLv2-only (not "or later") prohibits linking. Must be a dynamically-loaded plugin with C ABI boundary. Not deferrable -- must be architectured into Phase 3 design from the start.

### HIGH Findings

**F2.3: RenderBackend interface incomplete.**
Missing: cursor rendering (DrawMouseCursor, cursor cache), animation buffer (GetAnimBuffer/ReleaseAnimBuffer for 40bpp blitter), sprite encoding (SpriteEncoder interface), dirty rect tracking, palette change callbacks.

**F2.4: Blitter contamination.**
Adding MV output to blitters violates separation of concerns. **Better approach: draw-command recording at GfxBlitter() level + GPU-side MV rasterization.** Records ~5000 (rect, dx, dy) tuples per frame (~100KB) instead of 8MB of per-pixel data. Zero blitter modifications. Works with all variants including 8bpp and SSE.

**F2.5: Driver proliferation.**
12 drivers * 10 blitters * 6 upscale modes = untestable. Fix: use composition inside existing drivers (render backend as component, not new driver subclass).

**F2.6: Alternative architectures not evaluated.**
GPU blitter (extending existing OpenGLSprite system to all sprites), SDL_GPU (SDL3), wgpu-native. May be superior to hand-written Vulkan + DX11.

**F2.7: Over-engineering.**
Phase 1 is sound. Phase 2 should be a 4-week research prototype with hard kill criteria. Phase 3 is premature -- requires Phase 2 success AND community demand AND licensing resolution.

### MEDIUM Findings

**F2.8: Backwards compatibility gaps.**
8bpp fallback path undocumented. NewGRF pixel-alignment under jitter needs testing.

---

## Review 3: Performance

### CRITICAL Findings

**F3.1: Per-pixel MV writes double CPU write bandwidth, destroy cache.**
Writing 4-byte MV per pixel adds 8.29 MB/frame at 1080p. Combined with color buffer, total exceeds L3 cache. Interleaved dual-stream writes in the blitter inner loop destroy the sequential access pattern. The plan dismissed this as "memcpy-level" -- incorrect.

**F3.2: PBO upload triples at high resolution.**
Color + MV + depth = 14.5 MB/frame at 1080p, 58 MB/frame at 4K. At 4K/60fps = 3.48 GB/s CPU-to-GPU. No dirty-rect optimization mentioned for MV/depth buffers.

**F3.3: SSE blitter inner loops incompatible with MV writes.**
SSE2 processes 2 pixels at a time with __m128i. Adding per-pixel MV writes requires breaking SIMD or maintaining parallel SIMD pipeline for MV. Register pressure on 32-bit x86 causes spilling. Template explosion doubles existing specialization count.

### HIGH Findings

**F3.4:** FSR 2 on OpenGL compute 3x slower on NVIDIA. At 1080p output: 3-6ms instead of 1-2ms. Exceeds 3ms budget.

**F3.5:** Frame Generation vs fixed tick rate causes judder. Game state updates at 33fps; Dynamic MFG generates variable frames that mismatch vsync. **Recommendation: remove Frame Gen from scope entirely.**

**F3.6:** Render scale CPU savings not clearly communicated. Primary value is quality (better than bilinear), not performance. Minimum scale should be 67%, not 50%.

### MEDIUM Findings

**F3.7:** Temporal upscaling latency during scroll/building. Fast scrolls disocclude content with no history. Building ghost sprites shimmer under jitter.

**F3.8:** GPU compute power draw on laptops. OpenTTD is CPU-light; adding GPU compute increases power 15-30W. Fans spin up. Battery life drops 15-25%. Features must default OFF with power profile settings.

---

## Review 4: Community/Licensing

### CRITICAL Findings

**F4.1: GPLv2 + DLSS proprietary DLLs is a legal certainty, not a risk.**
OpenTTD is GPLv2-only (confirmed in COPYING.md). The Streamline SDK headers are MIT but the DLSS inference engine (`nvngx_dlss.dll`) is proprietary. GPLv2 Section 2(b) requires the whole work be GPL-licensed. Dynamic linking creates a derivative work under FSF interpretation. The "mere aggregation" exception does not apply when OpenTTD code calls `slInit()`, `slDLSSSetOptions()` etc. Cannot vendor Streamline in `3rdparty/`. Must use plugin architecture modeled on existing `social_integration.cpp` (runtime `dlopen`/`LoadLibrary` with C ABI boundary).

**F4.2: Long-term maintenance burden unsustainable for volunteer team.**
The plan adds 5,750+ lines of GPU-specific code with vendor SDK dependencies across Vulkan, DX11, DLSS, FSR 2, FSR 3. Each SDK releases 2-4 updates/year. OpenTTD's git log shows focus on gameplay/translations/bugfixes, not graphics engine work. No maintainer identified for GPU backends. Recommendation: drop DX11 entirely (Vulkan works on Windows), gate Phase 3 on maintainer commitment, set 12-month deprecation policy for unmaintained backends.

### HIGH Findings

**F4.3: Community fragmentation -- two-tier player base.**
Hardware hierarchy from RTX 50 (DLSS + Frame Gen + FSR 3) down to software rendering (nothing). OpenTTD's community historically values gameplay over visuals. Phase 1 should be framed as a performance feature for low-end hardware (render scaling lets weak GPUs run at higher displays), not a stepping stone to DLSS.

**F4.4: FSR 4 licensing model shift.**
FSR 2/3 MIT is GPL-compatible (safe to vendor). FSR 4 moved to driver-based ML upscaler with pre-built signed DLLs -- may create same GPLv2 conflict as DLSS. Need explicit analysis. Keep FSR 2/3 open-source as primary path.

**F4.5: Platform coverage gaps.**
macOS MoltenVK is community-maintained, not Apple-blessed. FreeBSD, Haiku, Emscripten not mentioned. Plan needs explicit platform coverage table. Consider native Metal backend for macOS instead of MoltenVK dependency.

**F4.6: Build complexity from 4+ new SDK dependencies.**
Adding Vulkan SDK, SPIR-V compiler, Streamline, FSR SDKs dramatically increases contributor onboarding friction. All backends MUST be compile-time optional (`-DWITH_VULKAN=OFF`). Default build must succeed with zero new SDKs. Pre-compile SPIR-V shaders into repo.

### MEDIUM Findings

**F4.7: NewGRF sprite artifacts from post-processing.**
Sub-pixel jitter can break pixel-perfect tile alignment in NewGRF sprites. CAS sharpening exaggerates dithering patterns in 8bpp palette sprites. Need "pixel-perfect mode" bypass and NewGRF creator involvement in Phase 2 testing.

**F4.8: User-facing option complexity.**
6-way upscaling dropdown (DLSS/FSR 3/FSR 2/FSR 1/CAS/None) confuses transport sim players. Simplify to "Upscaling: Off / Performance / Balanced / Quality" with auto-selection. Show active technology as info text, not as a user choice.

---

## Cross-Review Consensus

All four reviews independently converged on these conclusions:

1. **Do not modify the blitter.** Motion vectors must be generated outside the pixel compositing path.
2. **Phase 1 is sound** and should proceed as-is.
3. **Phase 2 needs a prototype gate** before full commitment.
4. **DLSS licensing is a hard blocker** requiring upfront architectural decisions.
5. **Frame Generation is useless** for OpenTTD's fixed tick rate.
6. **All features must default to OFF** to avoid disrupting existing users.
