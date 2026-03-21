# OpenTTD Rendering Pipeline Analysis

**Date:** 2026-03-21
**Purpose:** Technical analysis of OpenTTD's rendering architecture for evaluating DLSS/FSR integration feasibility.

## 1. Architecture Overview

OpenTTD's rendering pipeline is fundamentally **CPU-based**. The GPU is used only as a presentation layer (uploading a finished framebuffer to screen). There is **no GPU-accelerated scene rendering**, no 3D pipeline, and no compute shader usage.

```
Game State -> Viewport Drawing (CPU) -> Blitter (CPU) -> Video Buffer (RAM)
    -> [Optional: OpenGL texture upload] -> Screen
```

## 2. Blitter System (`src/blitter/`)

The blitter is the core rendering engine. It operates entirely on CPU memory buffers.

### 2.1 Blitter Hierarchy

| Blitter | Depth | Description |
|---------|-------|-------------|
| `8bpp_simple` | 8-bit | Palette-indexed, simplest path |
| `8bpp_optimized` | 8-bit | Optimized 8bpp with RLE compression |
| `32bpp_simple` | 32-bit | RGBA, reference implementation |
| `32bpp_optimized` | 32-bit | RGBA with RLE-compressed sprites |
| `32bpp_sse2` | 32-bit | SSE2 SIMD-accelerated |
| `32bpp_ssse3` | 32-bit | SSSE3 SIMD-accelerated |
| `32bpp_sse4` | 32-bit | SSE4.1 SIMD-accelerated |
| `32bpp_anim` | 32-bit | 32bpp + palette animation tracking |
| `32bpp_anim_sse2` | 32-bit | Animated + SSE2 |
| `32bpp_anim_sse4` | 32-bit | Animated + SSE4 |
| `40bpp_anim` | 40-bit | 32bpp RGBA + 8bpp animation buffer (OpenGL only) |
| `null` | N/A | No-op for dedicated servers |

### 2.2 Blitter Interface (`src/blitter/base.hpp`)

Key operations are all CPU-memory based:
- `Draw()` - Blit a sprite onto the video buffer with mode (Normal, ColourRemap, Transparent, CrashRemap, BlackRemap)
- `DrawColourMappingRect()` - Apply palette-based colour remapping to a screen region
- `SetPixel()` / `DrawRect()` / `DrawLine()` - Primitive drawing to CPU buffer
- `ScrollBuffer()` - Scroll the video buffer contents (for viewport panning)
- `CopyFromBuffer()` / `CopyToBuffer()` - Buffer management
- `PaletteAnimate()` - Handle palette cycling animations
- `Encode()` - Encode sprites from loader format to blitter-specific format

### 2.3 The 40bpp Blitter

The `40bpp_anim` blitter (`src/blitter/40bpp_anim.hpp`) is specifically designed for the OpenGL video driver. It uses:
- 32 bits (RGBA) for colour in the main video buffer
- 8 bits for palette animation index in a **separate** animation buffer

This separation allows OpenGL to handle palette animation in a shader (remap program) rather than requiring CPU-side palette cycling of the entire framebuffer.

## 3. Video Driver System (`src/video/`)

### 3.1 Available Drivers

| Driver | Platform | GPU Accel | API |
|--------|----------|-----------|-----|
| `win32` (GDI) | Windows | No | Win32 GDI (BitBlt) |
| `win32-opengl` | Windows | Presentation only | OpenGL 3.2+ |
| `sdl2-default` | Cross-platform | No | SDL2 surface |
| `sdl2-opengl` | Cross-platform | Presentation only | OpenGL via SDL2 |
| `cocoa` | macOS | No | Cocoa/Quartz |
| `cocoa-opengl` | macOS | Presentation only | OpenGL via Cocoa |
| `allegro` | Cross-platform | No | Allegro |
| `dedicated` | All | No | None (headless) |
| `null` | All | No | None |

### 3.2 Driver Architecture

Base class: `VideoDriver` (`src/video/video_driver.hpp`)
- Manages game thread vs draw thread separation
- Tracks dirty rectangles for partial redraws
- Handles VSync through `uses_hardware_acceleration` flag
- Draw interval: configurable refresh rate or VSync-driven

### 3.3 Critical Observation: No Vulkan, No DirectX

There is **no Vulkan driver** and **no Direct3D/DirectX driver**. The only GPU-accelerated path is OpenGL, and it is used purely for presentation (framebuffer upload + fullscreen quad).

## 4. OpenGL Backend (`src/video/opengl.cpp`)

### 4.1 How It Works

The OpenGL backend is **not** a GPU renderer. It is a display mechanism:

1. **CPU renders** the entire frame into a Pixel Buffer Object (PBO) mapped to CPU memory
2. The PBO is uploaded to a **GL_TEXTURE_2D** (`vid_texture`)
3. A **fullscreen quad** is drawn with a simple fragment shader
4. The shader either:
   - Passes through RGBA directly (`vid_program`, `_frag_shader_direct`)
   - Performs palette lookup for 8bpp content (`pal_program`, `_frag_shader_palette`)
   - Composites RGBA + animation remap for 40bpp (`remap_program`, `_frag_shader_rgb_mask_blend`)

### 4.2 Shader Programs

All shaders are extremely simple GLSL 1.10 / 1.50:

| Program | Purpose | Complexity |
|---------|---------|------------|
| `vid_program` | Display 32bpp RGBA buffer | Texture sample only |
| `pal_program` | Display 8bpp with palette lookup | 1D palette texture lookup |
| `remap_program` | Display 40bpp (RGBA + anim) | Palette remap + brightness modulation |
| `sprite_program` | Render individual sprites (cursor) | Palette remap + crash greyscale |

### 4.3 Texture Configuration

```cpp
_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
```

**GL_NEAREST filtering** is used everywhere. No interpolation, no anti-aliasing, no mipmapping on the framebuffer texture.

### 4.4 No Framebuffer Objects (FBOs)

The OpenGL backend renders directly to the default framebuffer. There are **no off-screen render targets**, no multi-pass rendering, and no post-processing pipeline.

### 4.5 No Depth Buffer

```cpp
SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
```

Depth testing is explicitly disabled. The rendering is purely 2D compositing.

## 5. Sprite & Drawing Pipeline

### 5.1 Sprite Flow

```
GRF/PNG Files -> SpriteLoader -> SpriteCache -> Blitter::Encode() -> Encoded Sprites
    -> Blitter::Draw() -> Video Buffer (CPU RAM)
```

### 5.2 Viewport Rendering

The viewport system (`src/viewport.cpp`) determines what tiles and objects are visible, then calls drawing functions that ultimately invoke the blitter to composite sprites onto the CPU video buffer.

### 5.3 Zoom System (`src/zoom_type.h`)

Zoom levels range from `In4x` (4x zoom in) to `Out8x` (8x zoom out). Sprites are pre-rendered at multiple zoom levels and stored in the sprite cache. **No runtime scaling or interpolation occurs** - the appropriate pre-rendered zoom level sprite is selected and drawn pixel-perfectly.

## 6. What Does NOT Exist

The following features that would be needed for DLSS/FSR integration are completely absent:

- **No Vulkan or DirectX rendering path**
- **No GPU-accelerated scene rendering** (all rendering is CPU-side)
- **No motion vector generation** (no temporal data between frames)
- **No depth buffer** (2D isometric rendering, no Z-data)
- **No post-processing pipeline** (no FBO-based render passes)
- **No compute shader support**
- **No render-to-texture workflow** (beyond the final framebuffer upload)

## 7. Key Files Reference

| File | Purpose |
|------|---------|
| `src/blitter/base.hpp` | Blitter interface definition |
| `src/blitter/factory.hpp` | Blitter factory/selection |
| `src/blitter/32bpp_optimized.hpp` | Primary 32bpp blitter |
| `src/blitter/40bpp_anim.hpp` | OpenGL-specific blitter |
| `src/video/video_driver.hpp` | Video driver base class |
| `src/video/opengl.h` / `.cpp` | OpenGL backend |
| `src/video/sdl2_opengl_v.cpp` | SDL2+OpenGL video driver |
| `src/video/win32_v.h` / `.cpp` | Win32 video drivers (GDI + OpenGL) |
| `src/table/opengl_shader.h` | GLSL shader source code |
| `src/gfx.cpp` | High-level drawing functions |
| `src/viewport.cpp` | Viewport rendering |
| `src/zoom_type.h` | Zoom level definitions |
