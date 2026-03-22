# GPU Auto-Detect Benchmark Plan

## Goal
Automatically determine the optimal post-processing preset for the player's GPU.

## Benchmark Methodology

### Phase 1: Capability Detection (instant)
- Check OpenGL version (3.2 minimum, 4.3+ for compute)
- Check available extensions (GL_ARB_compute_shader, GL_ARB_shader_storage_buffer)
- Check max texture size and max uniform buffer size

### Phase 2: Shader Stress Test (3-5 seconds)
Run progressive shader passes and measure frame time:

1. **Baseline** (5 frames): Render with PP off, measure base frame time
2. **Light** (5 frames): Enable FXAA only, measure delta
3. **Medium** (5 frames): Enable FXAA + bloom + vignette, measure delta
4. **Heavy** (5 frames): Enable realistic preset (shadows, SSAO, water, sway), measure delta
5. **Maximum** (5 frames): Enable all effects, measure delta

### Phase 3: Tier Classification

| Frame Time (all effects) | GPU Tier | Recommended Preset |
|---|---|---|
| < 4ms | 1 (High-end) | realistic |
| 4-8ms | 2 (Mid-range) | sharp (FSR + FXAA) |
| 8-16ms | 3 (Low-end) | performance (CPU scale 50%) |
| > 16ms | 4 (Integrated) | clean (no effects) |

### Phase 4: Recommendation
Apply the recommended preset and show results to player:
```
GPU Benchmark Complete:
  Baseline: 2.1ms
  With all effects: 6.8ms
  GPU Tier: 2 (Mid-range)
  Recommended: 'sharp' preset
  Applied. Use 'pp preset <name>' to change.
```

## Future Enhancements
- Store benchmark results in openttd.cfg
- Re-benchmark when GPU driver updates
- Per-effect cost measurement for custom presets
- Automatic quality scaling (reduce effects if FPS drops below target)
