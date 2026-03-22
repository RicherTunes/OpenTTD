# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

OpenTTD uses CMake (minimum 3.16) with C++20. Default build type is Debug.

```bash
# Configure and build (Linux/macOS)
mkdir build && cd build
cmake ..
make -j$(nproc)

# Configure and build (Windows with MSVC + vcpkg)
mkdir build && cd build
cmake .. -G"Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="x64-windows-static"

# Release build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Dedicated server (no GUI)
cmake .. -DOPTION_DEDICATED=ON
```

## Running Tests

Unit tests use Catch2 (bundled in `src/3rdparty/catch2/`), run via CTest:

```bash
cd build
ctest -j$(nproc) --timeout 120
```

Set `CTEST_OUTPUT_ON_FAILURE=1` for verbose failure output.

Regression tests run the game with AI scripts and compare output:
```bash
cd build
make regression
```

## Coding Style (Key Rules)

Full details in `CODINGSTYLE.md`. The essentials:

- **Functions**: CamelCase, opening brace on new line
- **Variables**: lowercase_with_underscores; globals prefixed with `_`
- **Members**: always use `this->` to reference own class members
- **Pointers/refs**: symbol next to name (`Vehicle *v`, not `Vehicle* v`)
- **Indentation**: tabs only (no spaces for indentation); unlimited line length
- **Control flow**: space before parentheses (`if (`, `for (`); opening brace on same line
- **Single-statement if**: allowed on same line without braces, only if no `else` clause
- **Unconditional loops**: `for (;;) {`
- **Enums**: unscoped use ALL_CAPS_WITH_UNDERSCORES; scoped use CamelCase; special markers `_BEGIN`, `_END`, `INVALID_` (0xFF/0xFFFF/0xFFFFFFFF)
- **Comments**: `/* */` for single-line code comments; `//` only at end of code lines; `/** */` for Doxygen; `///<` for inline member docs
- **No trailing whitespace**; no non-ASCII characters even in comments
- **Templates**: use `.hpp` extension for template header files containing implementations
- **Null checks**: use explicit `!= nullptr`, not implicit truthiness
- **Preprocessor**: `#` in column 1, indent with tabs after `#`

## Commit Message Format

```
<keyword>( #<issue>|<commit>)?: ([<component>])? <details>
```

**Player-facing keywords**: Feature, Add, Change, Fix, Remove, Revert, Doc, Update
**Developer-facing keywords**: Codechange, Cleanup, Codefix

Details start with capital, no trailing dot. For Fix, include issue number (`#NNNNN`) and/or regression commit hash when known. Components: Network, NewGRF, Script, YAPF, MacOS, Linux, Windows, CI, CMake, etc.

Examples:
- `Fix #5926: [YAPF] Infinite loop in pathfinder`
- `Codechange: Refactor vehicle drawing to reduce duplication`
- `Codefix 80dffae: Warning about unsigned unary minus`

## Architecture Overview

### Game Loop (`src/openttd.cpp`)

`StateGameLoop()` runs per tick: calendar timers, economy timers, tile loop (`RunTileLoop()`), vehicle ticks (`CallVehicleTicks()`), AI/GameScript loops, window events, news.

### Command System (Deterministic Multiplayer)

All game state mutations go through the command system. Commands are test-executed locally, sent to the server, timestamped, broadcast to all clients, and executed deterministically at the same frame. This ensures multiplayer sync. Command handlers are in `*_cmd.cpp` files with declarations in `*_cmd.h`.

### Pool-Based Object Management

Game objects (Vehicle, Station, Company, Engine, Order, etc.) are stored in typed pools (`src/core/pool_type.hpp`) with `PoolID` strong typedefs. Objects are accessed by ID, not raw pointers, for serialization safety. Base structs are in `*_base.h` files.

### Map/Tile System

The map is a flat array of tiles. Each tile type has accessor functions in `*_map.h` headers (e.g., `rail_map.h`, `road_map.h`, `water_map.h`). `RunTileLoop()` iterates tiles for periodic updates.

### Vehicle Hierarchy

Base `Vehicle` struct in `vehicle_base.h` with specialized types: Train (`train.h`), RoadVehicle (`roadveh.h`), Ship (`ship.h`), Aircraft (`aircraft.h`). Each has `*_cmd.cpp` (commands), `*_gui.cpp` (UI).

### Scripting (AI & GameScript)

Uses Squirrel VM (bundled in `src/3rdparty/squirrel/`). AI scripts in `src/ai/`, GameScripts in `src/game/`, shared infrastructure in `src/script/`. Script API bindings in `src/script/api/`.

### NewGRF System (`src/newgrf/`)

Mod format for custom graphics/behavior. Action-based processing: `newgrf_act0_*.cpp` define properties per object type, `newgrf_act1.cpp`/`newgrf_act2/`/`newgrf_act3.cpp` handle sprite sets and lookup tables.

### Save/Load (`src/saveload/`)

Per-subsystem serializers in `*_sl.cpp` files. Version-tracked format with backward compatibility layer in `compat/`. `afterload.cpp` handles data migration from older save formats.

### Driver Architecture

Video (`src/video/`), sound (`src/sound/`), music (`src/music/`) use factory pattern for runtime backend selection. Blitters (`src/blitter/`) provide 8bpp, 32bpp, and SIMD-optimized rendering paths.

### Link Graph (`src/linkgraph/`)

Cargo distribution uses multi-commodity flow algorithm. Runs asynchronously in a separate thread via `LinkGraphJob`.

### GUI System

Window framework in `window_gui.h`. Widget definitions in `src/widgets/*_widget.h`. GUI implementations in `*_gui.cpp` files.

### Table Data (`src/table/`)

Static game data as header files: sprites, engines, town buildings, airport movements, station graphics, settings definitions.

### Timer System (`src/timer/`)

Multiple timer domains: `TimerGameCalendar` (in-game dates), `TimerGameEconomy` (economic calendar), `TimerGameTick` (frame-based), `TimerGameRealtime` (wall clock).

### Pathfinding (`src/pathfinder/`)

Primary pathfinder is YAPF (Yet Another Pathfinder) in `pathfinder/yapf/`. `water_regions.cpp` optimizes ship pathfinding.

## Key File Naming Conventions

- `*_base.h` - Core data structure/pool definition
- `*_cmd.h` / `*_cmd.cpp` - Command handlers (game state mutations)
- `*_gui.cpp` - GUI window implementation
- `*_map.h` - Tile map accessor functions
- `*_type.h` - Type definitions and enums
- `*_func.h` - Function declarations
- `*_widget.h` - Widget enum definitions for GUI windows
- `*_sl.cpp` - Save/load serialization
- `*.hpp` - Template implementation headers

## Development Approach

Always use a TDD (Test-Driven Development) approach: write tests first, then implement, then verify. This applies to all new features, bug fixes, and refactoring.

## GPU Post-Processing Pipeline (In Progress)

The GPU post-processing pipeline adds visual enhancement to OpenTTD's OpenGL backend.

### Current Status
- **Phase 1 (Complete):** FBO pipeline, 25+ shader effects, render scaling (50-200%), settings UI
- **Phase 2a (Complete):** Motion vector recording, tile-based compute shader rasterization, jitter sequence
- **Phase 2b (Complete):** Temporal accumulation shader, history buffer, full pipeline wiring
- **Phase 3 (Complete):** RenderBackend abstraction, DLSS/FSR plugin C ABI, plugin auto-discovery, plugin dispatch in pipeline
- **Quality Gate 2 (Pending):** Visual A/B comparison requires manual game testing

### Key Files
- `src/video/postprocess.h/.cpp` -- Post-processing config, dimension math, FSR/CAS constants
- `src/video/motion_vector.h/.cpp` -- Draw-command recording, tile-based spatial binning (MAX_COMMANDS=16384)
- `src/video/temporal_upscale.h/.cpp` -- Jitter sequence, temporal upscale interface
- `src/table/postprocess_shader.h` -- All GLSL shader source (25+ effects + compute + bicubic + pixel smooth)
- `src/video/opengl.h/.cpp` -- FBO pipeline, shader compilation, render dispatch
- `src/benchmark.h/.cpp` -- GPU benchmark harness with CSV export and statistics
- `src/tests/test_postprocess.cpp` -- 240+ postprocess tests (incl. 27 for new effects)
- `src/tests/test_motion_vector.cpp` -- 45+ motion vector tests
- `src/tests/test_temporal_upscale.cpp` -- 12 temporal upscale tests
- `src/video/render_backend.h` -- Abstract rendering backend interface (Vulkan/DX11 composition)
- `src/video/upscale_plugin.h/.cpp` -- DLSS/FSR plugin C ABI + runtime loader
- `src/tests/test_upscale_plugin.cpp` -- 10 plugin interface tests

### Console Commands
- `benchmark start [N] [warmup=M] [label=X]` -- Capture N frames of GPU timing to CSV
- `benchmark stop/abort/status` -- Control benchmark recording
- `pp status/on/off` -- Master post-processing toggle
- `pp info` -- Show GPU pipeline capabilities and loaded plugins
- `pp enable/disable <effect>` -- Toggle individual effects:
  - Original: fxaa, night, crt, vignette, tiltshift, grain, smooth, supersample, lighting, bloom, weather
  - New: shadows, water, ssao, terrain_smooth, tree_sway, sky, dof
- `pp set <param> <value>` -- Set numeric parameters (render_scale, sharpening, brightness, etc.)
  - Shadows: shadow_intensity (0-100), shadow_angle (0-359), shadow_length (1-30), shadow_softness (1-10)
  - Water: reflection_intensity (0-100), reflection_distortion (0-20)
  - SSAO: ssao_radius (1-15), ssao_intensity (0-100), ssao_samples (4-16)
  - Terrain: terrain_smooth_radius (1-5), terrain_smooth_strength (0-100)
  - Sway: tree_sway_amount (1-10), tree_sway_speed (10-100)
  - Sky: cloud_density (0-100), cloud_speed (0-100), sky_brightness (0-100)
  - DOF: dof_focus (0-100), dof_aperture (0-100), dof_range (0-100)
- `pp reset` -- Restore all PP settings to defaults
- `pp preset <name>` -- Apply effect presets:
  - Original: retro, cinematic, night, miniature, sharp, temporal, zoom, clean
  - New: realistic, fantasy, photo, stormy, postcard
- `pp_screenshot [filename]` -- Capture post-processed framebuffer to BMP
- `benchmark compare` -- A/B testing workflow guide

### Design Decisions
- All features default to OFF (zero overhead when disabled)
- Motion vectors generated via draw-command recording at ViewportDrawParentSprites level (no blitter modifications)
- Tile-based spatial binning (16x16) for efficient GPU compute dispatch
- 8bpp blitter guard prevents PP on palette-indexed buffers
- Cursor rendered after PP at display resolution (position corrected for render scaling)
- DLSS requires plugin architecture with C ABI boundary for GPLv2 compliance

### Settings Flow
Global variables (`_video_*` in `video_driver.cpp`) → synced per-frame in `Paint()` → `PostProcessConfig` struct → shader uniforms in `RenderPostProcess()`. Settings persisted in openttd.cfg via `misc_settings.ini`. GUI controls in `settings_gui.cpp` modify globals directly; all 30+ GPU sub-controls disabled when PP master toggle is off.

### Tech Debt Paid
- All uniform locations cached at init time (no per-frame string lookups)
- FBO ping-pong both at display resolution (prevents corruption with >2 passes)
- Config sync properly gates on PP master toggle
- Shader compilation failures logged per-shader with graceful fallback
- Division-by-zero guards on all constant computations
- FSR EASU con1.zw stores pixel dimensions (not reciprocal) for correct UV-to-texel math
- GPU timer queries double-buffered (read previous frame, no GPU stall)
- Benchmark harness uses std::atomic for thread-safe active flag + deferred auto-stop
- Bloom composite saves pre-bloom scene into history texture before blur destroys it
- EASU outlier taps use correct stretch factor (inv_stretch, not stretch)
- RCAS, bicubic, and CRT shaders clamp output to [0,1] for RGBA8 FBO safety
- Dawn/dusk timing uses Gaussian peaks at correct positions (0.25/0.75)
- Weather animation has independent time base from film grain
- Plugin loader validates required function pointers (init/shutdown/evaluate)
- Benchmark label and screenshot filename sanitized against path traversal
- PostProcessPassCount ordering aligned with RenderPostProcess execution order
- GL error drain loop at end of RenderPostProcess catches all queued errors
- PostProcessConfig has defaulted operator== for clean comparison
- ResetPPDefaults() helper eliminates duplicated reset code in console commands
- Bloom history texture allocated on demand (works without temporal mode)
- Plugin pass count gated on actual plugin availability
- Zero-dimension guards on RenderPostProcess and SetupPostProcessFBOs
- Scene cut detection resets temporal history on viewport jumps
- Auto-supersampling boosts render_scale at In4x/In2x zoom levels
- Pixel art smoothing shader with EPX-inspired edge detection
- FSR EASU con1.zw stores pixel dimensions (not reciprocal) for correct UV-to-texel math
- GPU timer queries double-buffered (read previous frame, no GPU stall)
- Blitter depth cached per Paint() frame (avoid repeated virtual calls)
- Film grain time base is member variable (not function-local static)
- Motion vectors activated from Paint() when compute shaders available
- Safety blit fallback when all PP shader programs fail (prevents black screen)
- Benchmark CSV reports all 28+ PP settings as metadata
- Bicubic upscale uses render-resolution texel pitch (not display-resolution)
- Render scale slider skips frame during resize to prevent FBO state corruption
- DoF blur shader clamps output to [0,1] for RGBA8 FBO safety
- SSAO uses luminance-based pseudo-depth (no separate depth buffer needed)
- Water reflection detects water by blue_excess = b - max(r,g) with smoothstep
- Tree sway uses green_excess detection with position-keyed sine phase
- Sky clouds use FBM noise with brightness/saturation-based sky detection
- Fake shadows cast directional edge-detection blur with configurable angle
- All 7 new effects default to OFF, persist in openttd.cfg, expose in GUI + console
- Named constants ROUGHNESS_BASE/ROUGHNESS_PER_SMOOTHNESS replace magic numbers in tgp.cpp
- HeightMapSmoothSlopes uses direct pointer arithmetic instead of accessor calls
- Precomputed tile cache freed on both normal and abort generation paths

## Map Generation Improvements

7 new terrain generation features with TDD tests, all defaulting to OFF for backward compatibility.

### Features
- **Continent Shapes** -- Multiplicative heightmap masks: Island, Archipelago, Fjords, Scattered, Peninsula
- **Improved Perlin Interpolation** -- Quintic smoothstep replacing linear interpolation for smoother terrain
- **Mountain Ranges** -- Random walk ridge lines with Gaussian falloff, spatial hash acceleration
- **Lake Generation** -- BFS depression detection above sea level, fills enclosed basins as river tiles
- **Natural Harbor Scoring** -- Ray-cast coastline concavity analysis for town/port placement bias
- **Biome System** -- Temperature-based snow/desert zones using altitude + spatial noise
- **Voronoi Town Placement** -- Grid-based Lloyd relaxation for even town distribution

### Key Files
- `src/tgp.cpp` -- Continent shapes, mountain ranges, improved Perlin, pipeline profiling
- `src/tgp_func.h` -- Exposed TGP functions (quintic smoothstep) for testing
- `src/landscape.cpp` -- Lake creation, harbor scoring, biome zones
- `src/lake_gen.cpp/.h` -- Lake detection via BFS flood fill
- `src/harbor_gen.cpp/.h` -- Coastline concavity scoring with lifecycle management
- `src/town_cmd.cpp` -- Voronoi town placement with Lloyd relaxation
- `src/genworld_cache.cpp/.h` -- Precomputed valid tile lists for tree/town generation
- `src/terrain_advanced_gui.cpp/.h` -- Terrain Options sub-window (8 dropdowns)
- `src/tests/test_terrain_gen.cpp` -- Terrain generation tests
- `src/tests/test_town_placement.cpp` -- Town placement tests

### Settings (SLV_TERRAIN_GENERATION_V2 = 365)
All stored in `GameCreationSettings` struct, persisted via `world_settings.ini`:
- `continent_shape` (ContinentShape enum: None/Island/Archipelago/Fjords/Scattered/Peninsula)
- `terrain_algorithm` (TerrainAlgorithm enum: Classic/ImprovedPerlin)
- `biome_model` (BiomeModel enum: Classic/TemperatureBased)
- `town_distribution` (TownDistribution enum: Random/Even)
- `amount_of_lakes` (0-3: None/Few/Normal/Many)
- `amount_of_mountain_ranges` (0-3: None/Few/Normal/Many)

### Generation Pipeline (updated order)
```
tgp.cpp: HeightMapGenerate → MountainRanges → HeightMapNormalize
  (Normalize: AdjustWaterLevel → ContinentShape → CoastLines → SmoothSlopes → SmoothCoasts → SmoothSlopes → SineTransform → Curves)
landscape.cpp: FixSlopes → Water → Lakes → HarborScores → Biomes → Rivers
genworld.cpp: BuildTileCache → Towns → Industries → FreeHarbors → Trees → FreeTileCache
```

## Performance Optimizations

### Proven Improvements (with benchmark tests)

| Optimization | Speedup | File | What |
|---|---|---|---|
| Vector pre-allocation | **29x** | viewport.cpp | Reserve sprite vector capacity across frames |
| Binned sprite sorter | **1.5x** | viewport.cpp | Spatial binning for topological sort overlap search |
| Sine LUT | **4x** | tgp.cpp | Precomputed table replaces sin() in HeightMapCurves |
| River bitset | **17x** | landscape.cpp | vector\<bool\> replaces unordered_set for visited tiles |
| Precomputed tile list | **5.9x** | genworld_cache.cpp | Eliminates rejection sampling for tree/town placement |
| CDF binary search | **1.2x** | industry_cmd.cpp | O(log N) industry type selection via cumulative distribution |
| Early viewport culling | **1.2x** | viewport.cpp | Skip slope calc for tiles above viewport |
| Tile slope cache | -- | viewport.cpp | Per-frame unordered_map cache for GetTilePixelSlope |
| SmoothSlopes pointer | -- | tgp.cpp | Direct pointer arithmetic, boundary separation |
| Cargo dedup | *(kept linear)* | economy.cpp | Benchmark proved hash set slower for small N |

### Key Files
- `src/tests/test_optimization_benchmarks.cpp` -- 9 benchmark tests with 1000+ assertions
- `src/viewport.cpp` -- Binned sorter, vector prealloc, slope cache, early culling
- `src/tgp.cpp` -- Sine LUT, SmoothSlopes optimization, Debug timing instrumentation
- `src/landscape.cpp` -- River bitset visited tracking
- `src/genworld_cache.cpp` -- Precomputed valid tile lists
- `src/industry_cmd.cpp` -- CDF binary search for industry type selection

### Debug Timing
Enable with `-d map=2` to see per-step TGP generation timing:
```
TGP: HeightMapGenerate: 45ms
TGP: MountainRanges: 12ms
TGP: Normalize: 38ms
TGP: TransferToTiles: 8ms
TGP: Total generation: 103ms
```

## Documentation

Every file must have a `/** @file */` Doxygen comment block or Doxygen will skip all entities in that file. Function docs go in `.cpp` files; inline docs go in `.h`/`.hpp` files.
