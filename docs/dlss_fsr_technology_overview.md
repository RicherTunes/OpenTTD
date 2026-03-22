# DLSS 5 & FSR Technology Overview

**Date:** 2026-03-21
**Purpose:** Technical overview of NVIDIA DLSS 5 and AMD FSR 4.x for evaluating integration into OpenTTD.

> **Note (2026-03-22):** This document covers the technology landscape. For implementation status, see `gpu_migration_plan_v2.md`. FSR 1 spatial upscaling and CAS sharpening are fully implemented. Temporal upscaling infrastructure is complete. DLSS/FSR 2+ integration is available via the plugin C ABI (`src/video/upscale_plugin.h`).

## 1. NVIDIA DLSS 5

### 1.1 What It Is

DLSS 5 was announced at GTC on March 16, 2026. It is NVIDIA's most significant graphics advancement since real-time ray tracing (2018). Unlike previous DLSS versions (which focused on upscaling and frame generation), **DLSS 5 introduces neural rendering** -- an AI model that enhances the visual fidelity of rendered frames with photorealistic lighting and materials.

DLSS 5 is **not an upscaler**. It is a **visual enhancement/transformation** system that takes existing rendered frames and applies AI-inferred improvements to lighting, materials, and surface detail.

### 1.2 Technical Inputs

DLSS 5 requires only:
- **2D rendered color frame** (the final rendered image)
- **Motion vectors** (per-pixel screen-space motion between frames)

DLSS 5 does **not** require:
- 3D geometry data
- Depth buffers
- PBR material properties
- Normal maps
- Any engine-internal 3D data

The AI model infers scene semantics (characters, hair, fabric, skin, environment lighting) entirely from the 2D color frame and motion vectors.

### 1.3 What It Does

The neural rendering model enhances:
- **Subsurface scattering** on skin
- **Fabric sheen** and textile detail
- **Hair** light interactions
- **Light-material interactions** across the scene
- **Global illumination** quality improvements

Results are described as "deterministic, temporally stable, and anchored to the game's content."

### 1.4 Developer Controls

DLSS 5 provides developers with:
- **Intensity control** -- how aggressively the AI enhances
- **Color grading** -- adjust the tonal output
- **Masking** -- exclude specific objects or screen regions from enhancement
- **Artistic intent preservation** -- controls to maintain unique game aesthetics

### 1.5 SDK & API Requirements

| Requirement | Detail |
|-------------|--------|
| SDK | NVIDIA Streamline Framework |
| Rendering APIs | **DirectX 11, DirectX 12, Vulkan** |
| OpenGL | **NOT supported** |
| GPU Hardware | NVIDIA RTX 50 series (confirmed). Possibly RTX 40 series. |
| Integration method | Similar to DLSS Frame Generation (swap chain interception) |

### 1.6 Current Status

- **Not yet released** -- launches Fall 2026
- GTC demo required 2x RTX 5090 (one for game, one for DLSS 5)
- NVIDIA confirms launch version will run on a single GPU
- 12+ games announced at launch (AAA titles: Assassin's Creed Shadows, Hogwarts Legacy, Starfield, etc.)

### 1.7 Controversies & Limitations

- **Art style override concerns**: DLSS 5 has been criticized for pushing photorealistic "beauty standards" that may override artistic intent in stylized games
- **Only trained for photorealism**: Current model is focused on photorealistic lighting -- non-photorealistic/stylized rendering is not its strength
- **Temporal stability**: Requires good motion vectors for stability between frames
- **GPU cost**: Heavy neural network inference -- currently needs high-end RTX hardware

## 2. AMD FSR 4.x

### 2.1 Current Version

AMD FSR 4.1 (released March 19, 2026) is the latest version. FSR 4 is a fundamental departure from previous FSR versions.

### 2.2 Key Changes from FSR 3.x

FSR 4 introduces **machine learning-based upscaling** (replacing the purely algorithmic approach of FSR 1-3). It includes:
- ML-based temporal upscaling
- Ray Regeneration (similar to DLSS Ray Reconstruction)
- Improved detail preservation at Ultra Performance mode

### 2.3 Hardware Requirements

| Requirement | Detail |
|-------------|--------|
| GPU | **AMD Radeon RX 9000 series ONLY** |
| OS | Windows 10/11 |
| API | **DirectX 12 only** |
| Vulkan | **Not supported** for FSR 4 (FSR 3.1 Vulkan titles are not compatible) |

**Critical limitation:** FSR 4 is RX 9000-exclusive. It is **not** cross-vendor like FSR 1-3 were.

### 2.4 SDK Integration

- Uses FidelityFX SDK 2.0
- Requires pre-built, signed `amd_fidelityfx_loader.dll`
- Driver-based upgrade path from FSR 3.1 titles (DX12 only, signed DLLs only)
- Unreal Engine plugin available (UE 5.2-5.7)

### 2.5 Required Inputs (FSR 4 Upscaling)

FSR 4 upscaling requires:
- Low-resolution color buffer
- Motion vectors (per-pixel)
- Depth buffer
- Reactive mask (optional, for transparency/particles)
- Exposure value

### 2.6 Open Source Status

FSR 4 was **briefly** open-sourced on GPUOpen but the code availability status has been contentious.

## 3. Previous DLSS/FSR Versions (For Context)

### 3.1 DLSS Super Resolution (DLSS 2/3)

Traditional temporal upscaling:
- Renders at lower resolution
- Uses motion vectors + depth + previous frames to reconstruct higher-resolution output
- Requires: color buffer, motion vectors, depth buffer, exposure
- Supported APIs: DX11, DX12, Vulkan
- RTX 20/30/40/50 series GPUs

### 3.2 DLSS Frame Generation (DLSS 3)

Generates entirely new intermediate frames:
- Requires: two sequential rendered frames + motion vectors
- Intercepts swap chain presentation
- RTX 40/50 series only

### 3.3 FSR 1 (Spatial Upscaling)

Simple spatial upscaler:
- Requires only: a rendered frame (no motion vectors, no depth)
- Works on any GPU, any vendor
- Single-pass post-process shader
- **Most compatible with OpenTTD's architecture**
- Quality: acceptable but inferior to temporal solutions

### 3.4 FSR 2/3 (Temporal Upscaling)

Temporal upscaling similar to DLSS SR:
- Requires: color buffer, motion vectors, depth buffer
- Cross-vendor (any GPU)
- DX11, DX12, Vulkan support

## 4. Alternative Technologies

### 4.1 Lossless Scaling (Third-Party Tool)

- Steam application that works with **any** game
- Applies upscaling/frame generation externally by capturing the game window
- Supports FSR 1, NVIDIA NIS, LS1 (ML-based), Anime4K
- **No game integration needed** -- works at OS/window level
- Frame generation works on any GPU

### 4.2 Anime4K

- Open-source, real-time anime/2D upscaling
- Specifically designed for 2D cel-shaded/animated content
- Better suited for 2D sprite-based games than DLSS/FSR
- Available as shader filters (GLSL/HLSL)

### 4.3 NVIDIA Image Scaling (NIS)

- Spatial upscaler (single-frame, no temporal data needed)
- Simple shader-based approach
- Cross-vendor (despite NVIDIA name)
- Could work with OpenGL via custom shader

### 4.4 Magpie

- Free, open-source alternative to Lossless Scaling
- Window-level upscaling with various algorithms
- No game integration required

## 5. Comparison Matrix for OpenTTD Context

| Technology | Needs Motion Vectors | Needs Depth | Needs DX/Vulkan | Cross-Vendor | 2D Suitable | Integration Effort |
|------------|---------------------|-------------|-----------------|-------------|-------------|-------------------|
| DLSS 5 | Yes | No | DX11/12/Vulkan | No (NVIDIA) | Poor | Massive |
| FSR 4 | Yes | Yes | DX12 only | No (RX 9000) | Poor | Massive |
| DLSS SR | Yes | Yes | DX11/12/Vulkan | No (NVIDIA) | Poor | Massive |
| FSR 2/3 | Yes | Yes | DX11/12/Vulkan | Yes | Poor | Massive |
| FSR 1 | No | No | Any (shader) | Yes | Fair | Moderate |
| NIS | No | No | Any (shader) | Yes | Fair | Low |
| Anime4K | No | No | Any (shader) | Yes | Good | Low |
| Lossless Scaling | No (external) | No | N/A (external) | Yes | Good | None |

Sources:
- [NVIDIA DLSS 5 Announcement](https://www.nvidia.com/en-us/geforce/news/dlss5-breakthrough-in-visual-fidelity-for-games/)
- [DLSS 5 FAQ](https://www.nvidia.com/en-us/geforce/forums/geforce-graphics-cards/5/583738/dlss-5-faq/)
- [DLSS 5 uses 2D frames + motion vectors only](https://www.tweaktown.com/news/110569/dlss-5-only-takes-2d-rendered-frames-and-motion-vectors-as-input-not-3d-game-engine-data-confirms-nvidia/index.html)
- [DLSS 5 art style concerns](https://www.pcgamer.com/software/ai/dlss-5-clearly-overwrites-game-characters-with-ai-beauty-standards-but-nvidia-says-devs-have-artistic-control/)
- [Tom's Hardware DLSS 5 preview](https://www.tomshardware.com/pc-components/gpus/we-got-a-first-look-at-nvidias-dlss-5-and-the-future-of-neural-rendering-at-gtc-the-results-can-be-impressive-but-theres-work-to-do)
- [AMD FSR SDK - GPUOpen](https://gpuopen.com/amd-fsr-sdk/)
- [AMD FSR 4 in 85+ games](https://gpuopen.com/news/amd-fsr4-over-85-games/)
- [AMD FSR 4.1 release](https://www.tomshardware.com/pc-components/gpu-drivers/amd-releases-fsr-4-1-for-rx-9000-series-gpus-new-update-delivers-better-ray-regeneration-finer-upscaled-detail-and-higher-fps)
- [NVIDIA Streamline SDK](https://github.com/NVIDIA-RTX/Streamline)
- [NVIDIA DLSS GitHub](https://github.com/NVIDIA/DLSS)
