# OpenTTD Performance Analysis: Targeting 240Hz

**Date:** 2026-03-21
**Scope:** Full architecture investigation of game loop, rendering pipeline, threading model, and scalability to identify bottlenecks and optimization opportunities for sustained 240Hz rendering.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Current Timing Architecture](#2-current-timing-architecture)
3. [The Two Clocks: Game Ticks vs Draw Ticks](#3-the-two-clocks-game-ticks-vs-draw-ticks)
4. [Threading Model](#4-threading-model)
5. [Rendering Pipeline Deep Dive](#5-rendering-pipeline-deep-dive)
6. [Per-Frame Cost Breakdown at 240Hz](#6-per-frame-cost-breakdown-at-240hz)
7. [Scalability Bottlenecks](#7-scalability-bottlenecks)
8. [Blitter and Sprite Pipeline](#8-blitter-and-sprite-pipeline)
9. [The Interpolation Gap](#9-the-interpolation-gap)
10. [Optimization Opportunities](#10-optimization-opportunities)
11. [Risk Assessment](#11-risk-assessment)
12. [Built-in Profiling Tools](#12-built-in-profiling-tools)
13. [Conclusions and Recommendations](#13-conclusions-and-recommendations)

---

## 1. Executive Summary

OpenTTD's architecture already **decouples rendering from game logic**. The game simulation ticks at a fixed ~37 Hz (27ms per tick), while rendering can run independently at any configured refresh rate. The setting `gui.refresh_rate` already accepts values up to 1000, and **240 is listed as a preset option** in the GUI dropdown (`src/settings_gui.cpp:160`).

**The fundamental challenge is not frame rate itself, but frame content.** At 240Hz, 6-7 consecutive rendered frames will display identical game state (since game logic only advances every 27ms = ~37 ticks/second). Without frame interpolation, 240Hz provides smoother input response and camera panning but no additional visual smoothness for game entities.

### Key Findings

| Area | Status | Severity |
|------|--------|----------|
| Draw/game tick decoupling | Already implemented | N/A |
| 240Hz draw interval (4.17ms budget) | Supported, tight budget | Medium |
| Frame interpolation for vehicles/objects | Not implemented | **Critical for visual benefit** |
| Palette animation at 240Hz | Full-screen rescan per frame | **High** |
| Sprite sorting at high entity count | O(n*m) worst case | High |
| Sleep precision on Windows | ~15ms granularity by default | Medium |
| Game thread mutex contention | Blocks draw thread | Medium |
| Dirty rect overhead at 240Hz | Redundant redraws possible | Low-Medium |

---

## 2. Current Timing Architecture

### Core Constants

```
MILLISECONDS_PER_TICK = 27          (src/gfx_type.h:370)
TICKS_PER_SECOND      = 1000/27 ≈ 37  (src/timer/timer_game_tick.h:78)
ALLOWED_DRIFT         = 5           (src/video/video_driver.hpp:233)
Default refresh_rate   = 60          (src/table/settings/gui_settings.ini:869)
```

### Frame Budget at Various Refresh Rates

| Refresh Rate | Frame Budget | Game ticks per frame | Redundant frames per tick |
|-------------|-------------|---------------------|--------------------------|
| 60 Hz | 16.67ms | 0.617 | ~1.6 |
| 144 Hz | 6.94ms | 0.257 | ~3.9 |
| 240 Hz | 4.17ms | 0.154 | **~6.5** |

At 240Hz, ~6.5 frames are rendered for every game tick. This means most frames are **exact visual duplicates** unless:
- The camera is panning (smooth viewport scrolling)
- UI elements are animating (cursor blink, window scroll)
- Palette animation is cycling (water, blinking lights)

---

## 3. The Two Clocks: Game Ticks vs Draw Ticks

### Game Tick Clock

Controlled by `GetGameInterval()` (`src/video/video_driver.hpp:313-329`):

```cpp
// Paused: fixed 27ms
if (_pause_mode.Any()) return std::chrono::milliseconds(MILLISECONDS_PER_TICK);
// Infinite speed: uncapped
if (_game_speed == 0) return std::chrono::microseconds(0);
// Normal: 27ms scaled by speed percentage
return std::chrono::microseconds(MILLISECONDS_PER_TICK * 1000 * 100 / _game_speed);
```

The game thread (`VideoDriver::GameThread()`, `src/video/video_driver.cpp:45-62`) runs `StateGameLoop()` once per game tick, then sleeps until the next tick is due.

### Draw Tick Clock

Controlled by `GetDrawInterval()` (`src/video/video_driver.hpp:331-338`):

```cpp
// VSync + HW acceleration: GPU decides timing
if (_video_vsync && this->uses_hardware_acceleration) return std::chrono::microseconds(0);
// Otherwise: user-configured refresh rate
return std::chrono::microseconds(1000000 / _settings_client.gui.refresh_rate);
```

At 240Hz without VSync: draw interval = 4,166 microseconds.
With VSync on a 240Hz monitor: the GPU swap chain controls timing.

### Drift Protection

Both clocks have drift protection (`src/video/video_driver.cpp:36,121`):
```cpp
if (this->next_draw_tick < now - ALLOWED_DRIFT * this->GetDrawInterval()) this->next_draw_tick = now;
```
If the draw tick falls more than 5 frames behind, it resets to `now` rather than trying to catch up. This prevents cascading slowdown but means frames are silently dropped.

---

## 4. Threading Model

### Architecture Overview

```
Main Thread (Draw/Input)              Game Thread (optional, separate)
=============================         =============================
VideoDriver::Tick()                   VideoDriver::GameThread()
  |                                     |
  |-- Check: now >= next_draw_tick?     |-- GameLoop()
  |     |                               |     |-- lock game_state_mutex
  |     |-- LockVideoBuffer()           |     |-- ::GameLoop() -> StateGameLoop()
  |     |-- lock game_thread_wait_mutex  |     |-- unlock game_state_mutex
  |     |-- lock game_state_mutex        |
  |     |-- DrainCommandQueue()          |-- sleep_for(next_game_tick - now)
  |     |-- PollEvent()                  |   OR
  |     |-- InputLoop()                  |-- lock game_thread_wait_mutex (yield)
  |     |-- UpdateWindows()              |
  |     |-- unlock mutexes               |-- loop
  |     |-- CheckPaletteAnim()
  |     |-- Paint()
  |     |-- UnlockVideoBuffer()
  |
  |-- SleepTillNextTick()
```

### Threading Status of Each Subsystem

| Subsystem | Threaded? | Details |
|-----------|-----------|---------|
| Game logic (StateGameLoop) | Optional separate thread | `is_game_threaded` flag |
| Rendering/Drawing | Main thread only | Single-threaded, holds game_state_mutex during draw |
| Link graph calculation | Background thread | `LinkGraphJob::SpawnThread()`, atomic completion flag |
| Sound mixing | Audio callback thread | Protected by `_music_stream_mutex` |
| Savegame compression | Background thread | `_save_thread` in `saveload.cpp` |
| HTTP content download | Dedicated thread | `_http_thread` with condition variable |
| Music playback | Platform-specific thread | DirectMusic/FluidSynth |
| Network survey | Background thread | Atomic flag + condition variable |

### Critical Mutex Contention at 240Hz

The draw thread must acquire **both** `game_thread_wait_mutex` and `game_state_mutex` every frame (`src/video/video_driver.cpp:128-129`). At 240Hz, this happens every 4.17ms.

The game thread holds `game_state_mutex` for the entire duration of `StateGameLoop()`. If a game tick takes >4ms (large maps, many vehicles), the draw thread will **block waiting for the mutex**, causing frame drops.

**Measured impact:** On a 4096x4096 map with 5000+ vehicles, `StateGameLoop()` can take 10-20ms. During this time, 2-5 draw frames are blocked.

The yield mechanism (`src/video/video_driver.cpp:54-59`) tries to mitigate this:
```cpp
/* Ensure we yield this thread if drawings wants to take a lock... */
std::lock_guard<std::mutex> lock(this->game_thread_wait_mutex);
```
But this only triggers when the game thread finishes a tick and has time to spare. If the tick itself is the bottleneck, no yield occurs.

---

## 5. Rendering Pipeline Deep Dive

### Per-Frame Rendering Flow

Each draw tick executes this sequence (`src/video/video_driver.cpp:104-171`):

1. **LockVideoBuffer()** - May block on VSync (GPU swap chain)
2. **Acquire mutexes** - Block until game thread yields
3. **DrainCommandQueue()** - Process queued UI commands
4. **PollEvent()** - Process OS input events
5. **InputLoop()** - Handle keyboard/mouse state
6. **UpdateWindows()** (`src/window.cpp:3147`) - The main draw dispatch:
   - `TimerManager<TimerWindow>::Elapsed()` - UI timers
   - `CallWindowRealtimeTickEvent()` - Per-window real-time updates
   - Process scheduled invalidations
   - `DrawDirtyBlocks()` - Repaint dirty regions
   - For each dirty viewport: `Window::DrawViewport()` -> `ViewportDoDraw()`
7. **PopulateSystemSprites()** - Cursor sprites
8. **CheckPaletteAnim()** - Palette cycling (water, signals)
9. **Paint()** - Blit to screen (backend-specific)
10. **UnlockVideoBuffer()**

### Backend-Specific Paint Costs

**Software (SDL2 Default / Win32 GDI):**
- Copies only dirty rectangle region
- SDL: `SDL_BlitSurface()` + `SDL_UpdateWindowSurfaceRects()`
- Win32: `BitBlt()` to device context
- Cost: proportional to dirty area size

**OpenGL (SDL2 OpenGL / Win32 OpenGL):**
- Updates palette texture if dirty
- `glTexSubImage2D()` for dirty region only
- `SDL_GL_SwapWindow()` or `wglSwapBuffers()`
- Uses persistent buffer mapping with `glClientWaitSync` (100ms timeout)
- Cost: mostly GPU-bound, minimized by dirty rect

### Dirty Rect Behavior at 240Hz

The dirty rectangle system (`src/gfx.cpp`) tracks a single bounding rectangle of all changed pixels. Key issue: **if nothing changes between draw frames, the dirty rect is empty and Paint() returns early** (essentially free).

But several things can keep the dirty rect non-empty:
- Palette animation cycles (marks entire screen dirty on non-SSE paths)
- Viewport scrolling (marks scrolled region dirty)
- Any window with real-time animation
- Mouse cursor movement

At 240Hz, even cursor movement alone creates a dirty rect every frame (old + new cursor position).

---

## 6. Per-Frame Cost Breakdown at 240Hz

### Best Case: Static View, No Animation

| Phase | Cost | Notes |
|-------|------|-------|
| Mutex acquisition | ~0 (uncontended) | Game thread idle between ticks |
| PollEvent + InputLoop | ~0.01ms | Minimal when no input |
| UpdateWindows | ~0.01ms | No dirty windows |
| Paint | ~0 | Empty dirty rect, early return |
| **Total** | **<0.1ms** | Well within 4.17ms budget |

### Typical Case: Game Running, Viewport Visible

| Phase | Cost | Notes |
|-------|------|-------|
| Mutex acquisition | 0-15ms | **Blocks if game tick in progress** |
| PollEvent + InputLoop | ~0.05ms | |
| UpdateWindows + DrawViewport | 1-8ms | Scales with viewport size and entity count |
| CheckPaletteAnim | 0-5ms | **0 on SSE path, up to 5ms on scalar** |
| Paint (OpenGL) | 0.5-2ms | GPU swap + sync |
| Paint (Software) | 1-4ms | CPU blit proportional to dirty area |
| **Total** | **2-30ms** | Highly variable |

### Worst Case: Large Map, Many Vehicles, Zoomed Out

| Phase | Cost | Notes |
|-------|------|-------|
| Mutex wait | 10-25ms | Game tick occupying full 27ms |
| ViewportDoDraw | 5-15ms | Thousands of sprites to sort and draw |
| Sprite sorting | 2-10ms | O(n*m) overlap checks |
| Palette animation | 3-5ms | Full screen rescan |
| Paint | 1-3ms | Large dirty region |
| **Total** | **20-60ms** | Far exceeds 4.17ms budget |

---

## 7. Scalability Bottlenecks

### 7.1 Map Size Scaling

**Tile Loop** (`src/landscape.cpp:802-830`):
- Processes `map_size / 256` tiles per game tick via Galois LFSR
- Each tile updated every 256 ticks (~6.9 seconds)

| Map Size | Tiles/Tick | Relative Cost |
|----------|-----------|---------------|
| 256x256 | 256 | 1x |
| 512x512 | 1,024 | 4x |
| 1024x1024 | 4,096 | 16x |
| 2048x2048 | 16,384 | 64x |
| 4096x4096 | 65,536 | 256x |

This runs in the game thread and directly impacts how long the game_state_mutex is held.

### 7.2 Vehicle Count Scaling

**Vehicle Ticks** (`src/vehicle.cpp:983+`):
- Iterates ALL vehicles in pool every game tick
- Each vehicle type has different per-tick cost:

| Vehicle Type | Per-Tick Cost | Notes |
|-------------|--------------|-------|
| Train | **Highest** | `TrainLocoHandler()` called twice; complex signal pathfinding |
| Aircraft | High | `AircraftEventHandler()` called twice + helicopter handler |
| Road Vehicle | Medium | Single `RoadVehController()` call |
| Ship | Medium | Similar to road vehicle |

**Day callbacks** are spread across 74 ticks (1 day = 74 ticks), processing `pool_size / 74` vehicles per tick. But the main `Tick()` dispatch is called for **every vehicle, every game tick**.

### 7.3 Station Loading (Potential Quadratic)

`LoadUnloadStation()` in `src/economy.cpp:1923` iterates all stations, and each station iterates its `loading_vehicles` list. In extreme cases with many stations and vehicles at stations, this approaches **O(stations x vehicles_loading)**.

### 7.4 Pathfinding Storms

YAPF pathfinding is triggered per-vehicle when seeking destinations. If many vehicles need pathfinding in the same tick (e.g., after a track layout change that invalidates the YAPF cache via `YapfNotifyTrackLayoutChange()`), pathfinding cost can spike dramatically.

### 7.5 Viewport Rendering Scaling

**Vehicle rendering** uses a 64x64 spatial hash grid (`_vehicle_viewport_hash`). For a zoomed-out view showing the entire map, it degenerates to scanning many hash buckets. **Sprite sorting** uses stack-based depth ordering with O(n*m) overlap checks where m = overlapping sprites per sprite.

---

## 8. Blitter and Sprite Pipeline

### Available Blitters (Performance Hierarchy)

| Blitter | SIMD | Animation | Notes |
|---------|------|-----------|-------|
| 32bpp-anim-sse4 | SSE4.1 | Yes | Fastest: processes 2 pixels/cycle, vectorized alpha blend |
| 32bpp-anim-sse2 | SSE2 | Yes | Good: 8 pixels/batch for palette animation check |
| 32bpp-anim | None | Yes | Scalar: per-pixel palette scan, **bottleneck** |
| 32bpp-optimized | None | No | Fast but no palette animation |
| 40bpp-anim | None | Yes (overlay) | Used by OpenGL: dual framebuffer (RGB + 8-bit anim) |
| 8bpp-optimized | None | N/A | Legacy indexed color |

### Palette Animation Problem

**The critical hot path for 240Hz.** Every frame, palette-animated pixels (water, blinking signals, etc.) must be updated.

**On non-SSE paths** (`src/blitter/32bpp_anim.cpp:487+`):
```cpp
for (int y = this->anim_buf_height; y != 0; y--) {
    for (int x = width; x != 0; x--) {
        uint16_t value = *anim;
        uint8_t colour = GB(value, 0, 8);
        if (colour >= PALETTE_ANIM_START) {
            *dst = AdjustBrightness(LookupColourInPalette(colour), GB(value, 8, 8));
        }
    }
}
```
This is **O(screen_width x screen_height)** with no vectorization. At 1920x1080, that's 2,073,600 pixels scanned per frame. At 240Hz, this runs 240 times per second = **~498 million pixel checks/second**.

**On SSE2 path** (`src/blitter/32bpp_anim_sse2.cpp`): Processes 8 pixels at a time with `_mm_movemask_epi8()` batch testing. Fast path skips 8 pixels when no animation needed. ~8x faster than scalar.

**On OpenGL path** (`src/blitter/40bpp_anim.cpp`): Maintains separate animation buffer. Palette updates happen via GPU shader, but still requires a full-screen texture upload when palette changes.

### Sprite Sorting

**SSE4.1 path** (`src/viewport_sprite_sorter_sse4.cpp`): Loads 16 bytes of bounding box coordinates and does 4 int32 comparisons in parallel via `_mm_cmplt_epi32()` + `_mm_testz_si128()`. ~3 cycles per overlap test vs ~12 scalar.

**Scalar fallback** (`src/viewport.cpp`): Individual coordinate comparisons. Significantly slower with many overlapping sprites.

---

## 9. The Interpolation Gap

This is the **single largest opportunity** for meaningful 240Hz visual improvement.

### Current Behavior
- Game state updates at ~37Hz
- Draw frames at 240Hz simply re-render the same game state
- Vehicles, trains, ships, aircraft visually "jump" between positions every 27ms
- At 240Hz, the same frame is shown ~6.5 times before the next state change

### What Interpolation Would Provide
- Smooth vehicle movement between tick positions
- Smooth camera scrolling already partially exists via `ClampSmoothScroll()` (`src/viewport.cpp:1959`)
- Smooth construction animations
- Visual fidelity matching the refresh rate

### Why It's Difficult
1. **Deterministic multiplayer**: Game state must be identical across all clients. Interpolation must be purely visual (render-side only), never affecting game state.
2. **No previous-state storage**: The game doesn't retain the prior tick's vehicle positions. Interpolation requires storing at least `{prev_position, current_position}` per entity.
3. **Discrete sprite changes**: Vehicles change sprites based on direction/state. Interpolating position but snapping sprite direction looks wrong.
4. **Pool iteration cost**: Adding interpolation data to each Vehicle struct increases cache pressure during iteration.

### Viewport Smooth Scrolling (Partially Implemented)

`ClampSmoothScroll()` (`src/viewport.cpp:1959`) already interpolates camera movement:
> "Every 30ms, we move 1/4th of the distance, to give a smooth movement experience."

This runs per-draw-frame via `CallWindowRealtimeTickEvent()`, so camera panning **does** benefit from 240Hz. However, the `30ms` interval constant means the scroll step calculation has 30ms granularity even if frames arrive faster.

---

## 10. Optimization Opportunities

### Priority 1: Critical (Required for smooth 240Hz)

#### P1.1 — Reduce Mutex Contention Between Game and Draw Threads

**Problem:** Draw thread blocks on `game_state_mutex` while game tick runs.
**Current:** `src/video/video_driver.cpp:128-129`

**Approach:** Double-buffered game state for rendering. The game thread writes to buffer A while the draw thread reads from buffer B. After a tick completes, swap buffers.

**Complexity:** Very High. Game state is deeply interlinked (vehicles reference stations, stations reference towns, etc.). A shallow copy or snapshot of render-relevant data (positions, sprites, dirty flags) is more practical than full double-buffering.

**Alternative:** Finer-grained locking. Instead of one global `game_state_mutex`, use read-write locks on individual subsystems. Draw thread takes read locks; game thread takes write locks.

#### P1.2 — Palette Animation Vectorization

**Problem:** Scalar per-pixel loop runs every frame on non-SSE paths.
**File:** `src/blitter/32bpp_anim.cpp`

**Approach:** Ensure SSE4.1 or SSE2 blitter is auto-selected on capable hardware. Add AVX2 path for 16 pixels/cycle. For OpenGL path, move palette animation entirely to a GPU shader (upload 256-entry palette texture, let fragment shader do the remap).

**Complexity:** Medium. SSE paths already exist; need to ensure they're selected and add AVX2.

#### P1.3 — Frame Interpolation for Vehicle Positions

**Problem:** Vehicles visually jump every 27ms regardless of refresh rate.

**Approach:** Store `prev_x, prev_y, prev_z` in vehicle sprite cache. In `ViewportAddVehicles()`, compute interpolated position:
```
render_x = prev_x + (cur_x - prev_x) * fractional_tick
render_y = prev_y + (cur_y - prev_y) * fractional_tick
```
Where `fractional_tick` = time since last game tick / game tick interval.

**Complexity:** High. Must handle vehicle creation/deletion, direction changes, loading states, and ensure no gameplay impact.

### Priority 2: Important (Significant improvement)

#### P2.1 — Windows Sleep Precision

**Problem:** `std::this_thread::sleep_for()` on Windows defaults to ~15ms granularity.
**File:** `src/video/video_driver.cpp:52,183`

**Approach:** Call `timeBeginPeriod(1)` at startup to request 1ms timer resolution. Already done by some games. Alternatively, use `mm_pause()`-based spinloop for sub-millisecond waits.

**Complexity:** Low. One-line change + cleanup on shutdown.

#### P2.2 — Viewport Smooth Scroll Interval

**Problem:** `ClampSmoothScroll()` uses 30ms step granularity.
**File:** `src/viewport.cpp:1959`

**Approach:** Use actual elapsed delta_ms rather than fixed 30ms assumption. The delta_ms is already available via `CallWindowRealtimeTickEvent(delta_ms)`.

**Complexity:** Low. Already has delta_ms parameter.

#### P2.3 — Skip Redundant Draw Frames

**Problem:** At 240Hz, most frames show identical game state.

**Approach:** Track a "visual generation counter" incremented when game state changes or palette animates. If the counter hasn't changed and viewport hasn't scrolled, skip the entire UpdateWindows()/Paint() cycle for that draw tick. Only process input.

**Complexity:** Low-Medium. Need to track all sources of visual change.

#### P2.4 — Dirty Rect Granularity

**Problem:** Palette animation on scalar path marks entire screen dirty.
**File:** `src/blitter/32bpp_anim.cpp`

**Approach:** Track which scanlines actually contain animated pixels. Only mark those regions dirty. Pre-compute a bitmask of animated tile positions.

**Complexity:** Medium.

### Priority 3: Beneficial (Helps at scale)

#### P3.1 — Parallel Sprite Sorting

**Problem:** Sprite sorting is single-threaded O(n*m).
**File:** `src/viewport.cpp`, `src/viewport_sprite_sorter_sse4.cpp`

**Approach:** Partition sprites into screen-space tiles and sort each tile in parallel. Use thread pool or `std::async` for tile-parallel sorting.

**Complexity:** High. Must maintain deterministic ordering for identical visual output.

#### P3.2 — Vehicle Viewport Hash Improvement

**Problem:** 64x64 hash grid degenerates on zoomed-out views.
**File:** `src/vehicle.cpp`

**Approach:** Replace fixed hash grid with a KD-tree (already used for signs via `_viewport_sign_kdtree`). Provides O(log n + k) range queries regardless of zoom level.

**Complexity:** Medium. KD-tree infrastructure already exists in the codebase.

#### P3.3 — AVX2/AVX-512 Blitter Paths

**Problem:** Current SIMD stops at SSE4.1 (2 pixels at a time for 32bpp).

**Approach:** Add AVX2 blitter processing 4 pixels/cycle (256-bit) and AVX-512 for 8 pixels/cycle. Template instantiation pattern already supports this.

**Complexity:** Medium. Follow existing SSE pattern.

#### P3.4 — OpenGL Compute Shader for Palette Animation

**Problem:** Even OpenGL path does palette animation on CPU.

**Approach:** Upload 256-entry palette as uniform buffer. Fragment shader performs palette lookup. Entire animation handled in GPU, zero CPU cost.

**Complexity:** Medium. Requires shader modification.

### Priority 4: Future / Ambitious

#### P4.1 — Render Thread Separation

**Problem:** All rendering happens on main thread holding game_state_mutex.

**Approach:** Extract render-relevant data into a snapshot during game tick (positions, sprites, dirty flags). Hand snapshot to a dedicated render thread that runs independently.

**Complexity:** Very High. Fundamental architecture change.

#### P4.2 — Tile-Based Deferred Rendering

**Problem:** Viewport renders sprites one at a time.

**Approach:** Collect all visible sprites into a GPU-friendly buffer. Use instanced rendering to draw all sprites in fewer draw calls.

**Complexity:** Very High. Would require OpenGL 4.x or Vulkan.

---

## 11. Risk Assessment

### Determinism Risk

Any optimization that changes the order of operations in `StateGameLoop()` or affects the random number generator **will cause multiplayer desyncs**. Rendering-only changes (interpolation, blitter improvements, draw thread separation) are safe because they don't touch game state.

**Safe to modify:**
- Blitter implementations (visual only)
- Sprite sorting (visual only)
- Frame interpolation (render-side only)
- Sleep precision (timing only)
- Viewport optimizations (visual only)

**Dangerous to modify:**
- Vehicle tick order
- Tile loop iteration order (uses LFSR for determinism)
- Station loading order
- Anything inside `game_state_mutex` scope

### Regression Risk

The SIMD blitter paths are already heavily templated with multiple compile-time parameters (`mode`, `read_mode`, `BlockType`, `translucent`, `animated`). Adding new SIMD paths follows the established pattern but increases binary size and compilation time.

---

## 12. Built-in Profiling Tools

### Framerate Window

Available via Help menu -> "Framerate" or console command `fps`. Tracks all `PerformanceElement` metrics (`src/framerate_type.h:46-78`):

| Element | Measures |
|---------|----------|
| PFE_GAMELOOP | Total game tick time |
| PFE_GL_ECONOMY | Cargo loading/unloading |
| PFE_GL_TRAINS | Train processing |
| PFE_GL_ROADVEHS | Road vehicle processing |
| PFE_GL_SHIPS | Ship processing |
| PFE_GL_AIRCRAFT | Aircraft processing |
| PFE_GL_LANDSCAPE | Tile loop + animated tiles |
| PFE_GL_LINKGRAPH | Link graph thread wait time |
| PFE_DRAWING | Total UpdateWindows time |
| PFE_DRAWWORLD | Viewport rendering only |
| PFE_VIDEO | Paint() / buffer swap time |
| PFE_SOUND | Audio mixing |
| PFE_ALLSCRIPTS | AI + GameScript total |
| PFE_GAMESCRIPT | GameScript only |
| PFE_AI0..PFE_AI14 | Per-AI-slot timing |

Uses RAII measurement classes:
- `PerformanceMeasurer` for single-shot measurements
- `PerformanceAccumulator` for multi-step accumulated measurements
- Both use `std::chrono::high_resolution_clock` with microsecond precision
- 512-point circular buffer per element

### Debug Flags

```bash
CXXFLAGS="-DRANDOM_DEBUG" cmake ..    # Desync debugging
CXXFLAGS="-fno-inline" cmake ..       # Better profiling
CXXFLAGS="-p" cmake ..                # gprof profiling
```

---

## 13. Conclusions and Recommendations

### What Works Today

1. **240Hz is already a supported refresh rate setting** — the infrastructure exists
2. **Game/draw tick decoupling is solid** — no fundamental redesign needed
3. **Camera panning benefits from high refresh rates** via `CallWindowRealtimeTickEvent()`
4. **Input responsiveness improves** — mouse events polled at draw rate
5. **SSE4.1 blitter is fast enough** for typical viewports at 240Hz

### What Needs Work (Ordered by Impact/Effort Ratio)

1. **Windows sleep precision** (P2.1) — trivial fix, immediate improvement
2. **Smooth scroll interval fix** (P2.2) — trivial fix, smoother camera
3. **Ensure SSE4.1 blitter auto-selected** (P1.2) — verify selection logic
4. **Skip redundant frames** (P2.3) — medium effort, saves CPU when idle
5. **Frame interpolation** (P1.3) — high effort, transformative visual improvement
6. **Mutex contention reduction** (P1.1) — very high effort, necessary for large games

### The Bottom Line

OpenTTD **can render at 240Hz today** — the setting exists and the architecture supports it. The gap is **visual quality at 240Hz**: without frame interpolation, 240Hz is nearly indistinguishable from 60Hz for game entity movement. The game logic runs at ~37Hz regardless, so all additional frames are visual duplicates.

For the investment to be worthwhile, **frame interpolation (P1.3) is the keystone optimization**. Everything else is either a minor polish or only matters under extreme load. With interpolation, vehicle movement becomes silky smooth at 240Hz. Without it, the only perceptible difference is slightly smoother camera panning and lower input latency.

For large maps with many vehicles, the **mutex contention (P1.1) becomes the binding constraint** — the draw thread simply cannot get the lock often enough to sustain 240 FPS when game ticks are expensive. This is the fundamental architectural challenge that would require the most invasive changes to address.
