/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file postprocess_shader.h GLSL shader programs for GPU post-processing pipeline. */

/* ---- Vertex shader shared by all post-processing passes ---- */

/** Fullscreen triangle vertex shader for post-processing. */
static const char *_vertex_shader_pp[] = {
	"#version 150\n",
	"out vec2 tex_coord;",
	"void main() {",
	"  float x = -1.0 + float((gl_VertexID & 1) << 2);",
	"  float y = -1.0 + float((gl_VertexID & 2) << 1);",
	"  tex_coord = vec2(x, y) * 0.5 + 0.5;",
	"  gl_Position = vec4(x, y, 0.0, 1.0);",
	"}",
};

/* ---- Simple blit (passthrough) ---- */

/** Fragment shader that copies the source texture without modification. */
static const char *_frag_shader_pp_blit[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  frag_colour = texture(source_tex, tex_coord);",
	"}",
};

/* ---- AMD FidelityFX CAS (Contrast Adaptive Sharpening) ---- */

/** Fragment shader for CAS sharpening pass. */
static const char *_frag_shader_pp_cas[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform float sharpness;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"  vec3 n  = texture(source_tex, tex_coord + vec2( 0.0, -texel_size.y)).rgb;",
	"  vec3 s  = texture(source_tex, tex_coord + vec2( 0.0,  texel_size.y)).rgb;",
	"  vec3 e  = texture(source_tex, tex_coord + vec2( texel_size.x,  0.0)).rgb;",
	"  vec3 w  = texture(source_tex, tex_coord + vec2(-texel_size.x,  0.0)).rgb;",
	"  vec3 mn = min(c, min(min(n, s), min(e, w)));",
	"  vec3 mx = max(c, max(max(n, s), max(e, w)));",
	"  vec3 d  = mx - mn;",
	"  vec3 amp = clamp(d / max(mx, vec3(1e-5)), 0.0, 1.0);",
	"  amp = sqrt(amp);",
	"  float peak = -3.0 * sharpness + 8.0;",
	"  vec3 wt = amp / peak;",
	"  vec3 result = (c + (n + s + e + w) * wt) / (1.0 + 4.0 * wt);",
	"  frag_colour = vec4(result, src_alpha);",
	"}",
};

/* ---- AMD FidelityFX FSR 1.0 EASU (Edge Adaptive Spatial Upsampling) ---- */

/**
 * Fragment shader implementing FSR 1.0 EASU.
 *
 * Performs a 12-tap edge-adaptive spatial upsampling using a Lanczos2-like
 * kernel that adapts its shape based on detected edge direction. The filter
 * is sharper along edges and smoother perpendicular to edges.
 *
 * Sampling pattern (relative to center texel):
 *        b
 *      e f g
 *      h i j    (i = center pixel being computed)
 *      k l m
 *        n
 *
 * Uniforms:
 *   easu_con0 - viewport-to-input scale (xy) and offset (zw)
 *   easu_con1 - reciprocal input texel size (xy), input size in pixels (zw)
 *   easu_con2 - reserved for gather-based EASU variant
 *   easu_con3 - reserved for gather-based EASU variant
 */
static const char *_frag_shader_pp_fsr_easu[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec4 easu_con0;",
	"uniform vec4 easu_con1;",
	"uniform vec4 easu_con2;",
	"uniform vec4 easu_con3;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"",
	"/* Compute luma from linear RGB. */",
	"float EasuLuma(vec3 c) {",
	"  return dot(c, vec3(0.299, 0.587, 0.114));",
	"}",
	"",
	"/* Lanczos2-like weight function: (25/16)(2/5 x^2 - 1)^2 - (25/16-1) */",
	"/* Approximates Lanczos2 without requiring sin/pi. */",
	"float EasuWeight(float dist2) {",
	"  float x2 = dist2;",
	"  float a = x2 * (2.0 / 5.0) - 1.0;",
	"  float w = a * a * (25.0 / 16.0) - (25.0 / 16.0 - 1.0);",
	"  return max(w, 0.0);",
	"}",
	"",
	"void main() {",
	"  /* Map output pixel to input pixel position. */",
	"  vec2 input_pos = tex_coord * easu_con0.xy + easu_con0.zw;",
	"  vec2 input_texel = input_pos * easu_con1.zw;",
	"  vec2 center = floor(input_texel - 0.5) + 0.5;",
	"  vec2 fract_pos = input_texel - center;",
	"  vec2 texel_size = easu_con1.xy;",
	"  vec2 center_uv = center * texel_size;",
	"",
	"  /* Sample the 12 texels in the cross/diamond pattern. */",
	"  /*        b         */",
	"  /*      e f g       */",
	"  /*      h i j       */",
	"  /*      k l m       */",
	"  /*        n         */",
	"  vec3 texel_b = texture(source_tex, center_uv + vec2( 0.0, -1.0) * texel_size).rgb;",
	"  vec3 texel_e = texture(source_tex, center_uv + vec2(-1.0,  0.0) * texel_size).rgb;",
	"  vec3 texel_f = texture(source_tex, center_uv + vec2( 0.0,  0.0) * texel_size).rgb;",
	"  vec3 texel_g = texture(source_tex, center_uv + vec2( 1.0,  0.0) * texel_size).rgb;",
	"  vec3 texel_h = texture(source_tex, center_uv + vec2(-1.0,  1.0) * texel_size).rgb;",
	"  vec3 texel_i = texture(source_tex, center_uv + vec2( 0.0,  1.0) * texel_size).rgb;",
	"  vec3 texel_j = texture(source_tex, center_uv + vec2( 1.0,  1.0) * texel_size).rgb;",
	"  vec3 texel_k = texture(source_tex, center_uv + vec2(-1.0,  2.0) * texel_size).rgb;",
	"  vec3 texel_l = texture(source_tex, center_uv + vec2( 0.0,  2.0) * texel_size).rgb;",
	"  vec3 texel_m = texture(source_tex, center_uv + vec2( 1.0,  2.0) * texel_size).rgb;",
	"  vec3 texel_n = texture(source_tex, center_uv + vec2( 0.0,  3.0) * texel_size).rgb;",
	"",
	"  /* Sample source alpha from the nearest texel for passthrough. */",
	"  float src_alpha = texture(source_tex, tex_coord).a;",
	"",
	"  /* Compute per-texel luma for edge detection. */",
	"  float luma_b = EasuLuma(texel_b);",
	"  float luma_e = EasuLuma(texel_e);",
	"  float luma_f = EasuLuma(texel_f);",
	"  float luma_g = EasuLuma(texel_g);",
	"  float luma_h = EasuLuma(texel_h);",
	"  float luma_i = EasuLuma(texel_i);",
	"  float luma_j = EasuLuma(texel_j);",
	"  float luma_k = EasuLuma(texel_k);",
	"  float luma_l = EasuLuma(texel_l);",
	"  float luma_m = EasuLuma(texel_m);",
	"  float luma_n = EasuLuma(texel_n);",
	"",
	"  /* Analyze horizontal and vertical luma gradients for edge detection. */",
	"  /* Use the 3x3 neighborhood around the 4 bilinear taps (f,g,i,j). */",
	"  float grad_h_top = abs(luma_e - luma_g);",
	"  float grad_h_bot = abs(luma_h - luma_j) + abs(luma_k - luma_m);",
	"  float grad_v_left = abs(luma_e - luma_k);",
	"  float grad_v_right = abs(luma_g - luma_m) + abs(luma_f - luma_l);",
	"  float grad_h = grad_h_top + grad_h_bot;",
	"  float grad_v = grad_v_left + grad_v_right;",
	"",
	"  /* Compute edge direction: 0 = horizontal edge, 1 = vertical edge. */",
	"  /* A horizontal edge means luma changes vertically (grad_v > grad_h). */",
	"  float edge_h = step(grad_v, grad_h);",
	"  float edge_v = 1.0 - edge_h;",
	"",
	"  /* Determine directional length (stretch) along the detected edge. */",
	"  float len_h = (luma_f - luma_e) + (luma_f - luma_g);",
	"  float len_v = (luma_f - luma_b) + (luma_f - luma_l);",
	"  float dir_len = edge_h * len_h + edge_v * len_v;",
	"",
	"  /* Compute stretch factor - how elongated the filter kernel should be. */",
	"  /* More stretch along strong edges, less on flat areas. */",
	"  float stretch = clamp(abs(dir_len) * 4.0, 1.0, 2.0);",
	"  float inv_stretch = 1.0 / stretch;",
	"",
	"  /* For each of the 12 samples, compute the adapted filter weight. */",
	"  /* The kernel is elongated along the edge direction. */",
	"  /* Positions relative to the sub-pixel location within the center cell. */",
	"  vec2 pos_b = vec2( 0.0, -1.0) - fract_pos;",
	"  vec2 pos_e = vec2(-1.0,  0.0) - fract_pos;",
	"  vec2 pos_f = vec2( 0.0,  0.0) - fract_pos;",
	"  vec2 pos_g = vec2( 1.0,  0.0) - fract_pos;",
	"  vec2 pos_h = vec2(-1.0,  1.0) - fract_pos;",
	"  vec2 pos_i = vec2( 0.0,  1.0) - fract_pos;",
	"  vec2 pos_j = vec2( 1.0,  1.0) - fract_pos;",
	"  vec2 pos_k = vec2(-1.0,  2.0) - fract_pos;",
	"  vec2 pos_l = vec2( 0.0,  2.0) - fract_pos;",
	"  vec2 pos_m = vec2( 1.0,  2.0) - fract_pos;",
	"  vec2 pos_n = vec2( 0.0,  3.0) - fract_pos;",
	"",
	"  /* Apply directional stretching to distance computation. */",
	"  /* Along the edge direction: normal distance. */",
	"  /* Perpendicular to edge: compressed distance (sharper). */",
	"  float wb = EasuWeight((edge_h * pos_b.x * pos_b.x + edge_v * pos_b.y * pos_b.y) + (edge_v * pos_b.x * pos_b.x + edge_h * pos_b.y * pos_b.y) * stretch * stretch);",
	"  float we = EasuWeight((edge_h * pos_e.x * pos_e.x + edge_v * pos_e.y * pos_e.y) + (edge_v * pos_e.x * pos_e.x + edge_h * pos_e.y * pos_e.y) * inv_stretch * inv_stretch);",
	"  float wf = EasuWeight((edge_h * pos_f.x * pos_f.x + edge_v * pos_f.y * pos_f.y) + (edge_v * pos_f.x * pos_f.x + edge_h * pos_f.y * pos_f.y) * inv_stretch * inv_stretch);",
	"  float wg = EasuWeight((edge_h * pos_g.x * pos_g.x + edge_v * pos_g.y * pos_g.y) + (edge_v * pos_g.x * pos_g.x + edge_h * pos_g.y * pos_g.y) * inv_stretch * inv_stretch);",
	"  float wh = EasuWeight((edge_h * pos_h.x * pos_h.x + edge_v * pos_h.y * pos_h.y) + (edge_v * pos_h.x * pos_h.x + edge_h * pos_h.y * pos_h.y) * inv_stretch * inv_stretch);",
	"  float wi = EasuWeight((edge_h * pos_i.x * pos_i.x + edge_v * pos_i.y * pos_i.y) + (edge_v * pos_i.x * pos_i.x + edge_h * pos_i.y * pos_i.y) * inv_stretch * inv_stretch);",
	"  float wj = EasuWeight((edge_h * pos_j.x * pos_j.x + edge_v * pos_j.y * pos_j.y) + (edge_v * pos_j.x * pos_j.x + edge_h * pos_j.y * pos_j.y) * inv_stretch * inv_stretch);",
	"  float wk = EasuWeight((edge_h * pos_k.x * pos_k.x + edge_v * pos_k.y * pos_k.y) + (edge_v * pos_k.x * pos_k.x + edge_h * pos_k.y * pos_k.y) * inv_stretch * inv_stretch);",
	"  float wl = EasuWeight((edge_h * pos_l.x * pos_l.x + edge_v * pos_l.y * pos_l.y) + (edge_v * pos_l.x * pos_l.x + edge_h * pos_l.y * pos_l.y) * inv_stretch * inv_stretch);",
	"  float wm = EasuWeight((edge_h * pos_m.x * pos_m.x + edge_v * pos_m.y * pos_m.y) + (edge_v * pos_m.x * pos_m.x + edge_h * pos_m.y * pos_m.y) * inv_stretch * inv_stretch);",
	"  float wn = EasuWeight((edge_h * pos_n.x * pos_n.x + edge_v * pos_n.y * pos_n.y) + (edge_v * pos_n.x * pos_n.x + edge_h * pos_n.y * pos_n.y) * stretch * stretch);",
	"",
	"  /* Accumulate weighted sum. */",
	"  float total_weight = wb + we + wf + wg + wh + wi + wj + wk + wl + wm + wn;",
	"  vec3 result = (texel_b * wb + texel_e * we + texel_f * wf + texel_g * wg +",
	"                 texel_h * wh + texel_i * wi + texel_j * wj + texel_k * wk +",
	"                 texel_l * wl + texel_m * wm + texel_n * wn) / total_weight;",
	"",
	"  /* Clamp to the min/max of the 4 nearest samples to prevent ringing. */",
	"  vec3 near_min = min(min(texel_f, texel_g), min(texel_i, texel_j));",
	"  vec3 near_max = max(max(texel_f, texel_g), max(texel_i, texel_j));",
	"  result = clamp(result, near_min, near_max);",
	"",
	"  frag_colour = vec4(result, src_alpha);",
	"}",
};

/* ---- AMD FidelityFX FSR 1.0 RCAS (Robust Contrast Adaptive Sharpening) ---- */

/** Fragment shader for FSR 1.0 RCAS sharpening pass. */
static const char *_frag_shader_pp_fsr_rcas[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform float rcas_strength;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"  vec3 n = texture(source_tex, tex_coord + vec2( 0.0, -texel_size.y)).rgb;",
	"  vec3 s = texture(source_tex, tex_coord + vec2( 0.0,  texel_size.y)).rgb;",
	"  vec3 e = texture(source_tex, tex_coord + vec2( texel_size.x,  0.0)).rgb;",
	"  vec3 w = texture(source_tex, tex_coord + vec2(-texel_size.x,  0.0)).rgb;",
	"  vec3 mn = min(c, min(min(n, s), min(e, w)));",
	"  vec3 mx = max(c, max(max(n, s), max(e, w)));",
	"  vec3 hit_min = mn / max(4.0 * mx, vec3(1e-5));",
	"  vec3 hit_max = (1.0 - mx) / max(4.0 * mn + 4.0 * (1.0 - mx), vec3(1e-5));",
	"  float lobe = max(-0.1875, min(min(min(hit_min.r, hit_min.g), min(hit_min.b, hit_max.r)), min(hit_max.g, hit_max.b))) * rcas_strength;",
	"  vec3 result = (c + (n + s + e + w) * lobe) / (1.0 + 4.0 * lobe);",
	"  frag_colour = vec4(result, src_alpha);",
	"}",
};

/* ---- FXAA (Fast Approximate Anti-Aliasing) ---- */

/** Fragment shader implementing a simplified FXAA pass. */
static const char *_frag_shader_pp_fxaa[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform float subpix_quality;",
	"uniform float edge_threshold;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"",
	"float FxaaLuma(vec3 c) {",
	"  return dot(c, vec3(0.299, 0.587, 0.114));",
	"}",
	"",
	"void main() {",
	"  vec4 centerM = texture(source_tex, tex_coord);",
	"  float src_alpha = centerM.a;",
	"  vec3 rgbM  = centerM.rgb;",
	"  vec3 rgbNW = texture(source_tex, tex_coord + vec2(-1.0, -1.0) * texel_size).rgb;",
	"  vec3 rgbNE = texture(source_tex, tex_coord + vec2( 1.0, -1.0) * texel_size).rgb;",
	"  vec3 rgbSW = texture(source_tex, tex_coord + vec2(-1.0,  1.0) * texel_size).rgb;",
	"  vec3 rgbSE = texture(source_tex, tex_coord + vec2( 1.0,  1.0) * texel_size).rgb;",
	"",
	"  float lumaM  = FxaaLuma(rgbM);",
	"  float lumaNW = FxaaLuma(rgbNW);",
	"  float lumaNE = FxaaLuma(rgbNE);",
	"  float lumaSW = FxaaLuma(rgbSW);",
	"  float lumaSE = FxaaLuma(rgbSE);",
	"",
	"  float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));",
	"  float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));",
	"  float lumaRange = lumaMax - lumaMin;",
	"",
	"  if (lumaRange < max(0.0312, lumaMax * edge_threshold)) {",
	"    frag_colour = vec4(rgbM, src_alpha);",
	"    return;",
	"  }",
	"",
	"  vec2 dir;",
	"  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));",
	"  dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));",
	"  float dir_reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * subpix_quality, 1.0 / 128.0);",
	"  float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);",
	"  dir = clamp(dir * rcp_dir_min, vec2(-8.0), vec2(8.0)) * texel_size;",
	"",
	"  vec3 rgbA = 0.5 * (texture(source_tex, tex_coord + dir * (1.0 / 3.0 - 0.5)).rgb +",
	"                      texture(source_tex, tex_coord + dir * (2.0 / 3.0 - 0.5)).rgb);",
	"  vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(source_tex, tex_coord + dir * -0.5).rgb +",
	"                                     texture(source_tex, tex_coord + dir *  0.5).rgb);",
	"  float lumaB = FxaaLuma(rgbB);",
	"  vec3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;",
	"  frag_colour = vec4(result, src_alpha);",
	"}",
};

/* ---- Color grading ---- */

/** Fragment shader for color grading adjustments (brightness, contrast, saturation, temperature). */
static const char *_frag_shader_pp_color_grading[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float brightness;",
	"uniform float contrast;",
	"uniform float saturation;",
	"uniform float temperature;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"",
	"  /* Brightness: simple addition. */",
	"  c += brightness;",
	"",
	"  /* Contrast: scale around mid-grey. */",
	"  c = (c - 0.5) * contrast + 0.5;",
	"",
	"  /* Saturation: lerp towards luminance. */",
	"  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));",
	"  c = mix(vec3(luma), c, saturation);",
	"",
	"  /* Temperature: symmetric warm/cool shift. */",
	"  c.r += temperature * 0.1;",
	"  c.b -= temperature * 0.1;",
	"",
	"  frag_colour = vec4(clamp(c, 0.0, 1.0), src_alpha);",
	"}",
};

/* ---- Vignette ---- */

/** Fragment shader for vignette darkening at screen edges. */
static const char *_frag_shader_pp_vignette[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float vignette_strength;",
	"uniform float vignette_radius;",
	"uniform float vignette_softness;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"  vec2 uv = tex_coord - 0.5;",
	"  float dist = length(uv);",
	"  float v = smoothstep(vignette_radius, vignette_radius + vignette_softness, dist);",
	"  c *= 1.0 - v * vignette_strength;",
	"  frag_colour = vec4(c, src_alpha);",
	"}",
};

/* ---- Tilt-shift blur (horizontal pass) ---- */

/** Fragment shader for horizontal tilt-shift blur. */
static const char *_frag_shader_pp_tiltshift_h[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform float focus_y;",
	"uniform float focus_spread;",
	"uniform float blur_strength;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  float src_alpha = center.a;",
	"  float dist = abs(tex_coord.y - focus_y);",
	"  float blur_amount = smoothstep(0.0, focus_spread, dist) * blur_strength;",
	"  vec3 sum = vec3(0.0);",
	"  float weights[5] = float[](0.227027, 0.194594, 0.121622, 0.054054, 0.016216);",
	"  sum += center.rgb * weights[0];",
	"  for (int i = 1; i < 5; i++) {",
	"    vec2 off = vec2(float(i) * texel_size.x * blur_amount * 2.0, 0.0);",
	"    sum += texture(source_tex, tex_coord + off).rgb * weights[i];",
	"    sum += texture(source_tex, tex_coord - off).rgb * weights[i];",
	"  }",
	"  frag_colour = vec4(sum, src_alpha);",
	"}",
};

/* ---- Tilt-shift blur (vertical pass) ---- */

/** Fragment shader for vertical tilt-shift blur. */
static const char *_frag_shader_pp_tiltshift_v[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform float focus_y;",
	"uniform float focus_spread;",
	"uniform float blur_strength;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  float src_alpha = center.a;",
	"  float dist = abs(tex_coord.y - focus_y);",
	"  float blur_amount = smoothstep(0.0, focus_spread, dist) * blur_strength;",
	"  vec3 sum = vec3(0.0);",
	"  float weights[5] = float[](0.227027, 0.194594, 0.121622, 0.054054, 0.016216);",
	"  sum += center.rgb * weights[0];",
	"  for (int i = 1; i < 5; i++) {",
	"    vec2 off = vec2(0.0, float(i) * texel_size.y * blur_amount * 2.0);",
	"    sum += texture(source_tex, tex_coord + off).rgb * weights[i];",
	"    sum += texture(source_tex, tex_coord - off).rgb * weights[i];",
	"  }",
	"  frag_colour = vec4(sum, src_alpha);",
	"}",
};

/* ---- Night / desaturation ---- */

/** Fragment shader for night-time desaturation and blue-tint effect. */
static const char *_frag_shader_pp_night[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float night_amount;",
	"uniform float night_blue_shift;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));",
	"  /* Blue channel gets stronger tint based on night_blue_shift. */",
	"  float r_tint = 0.3 * (1.0 - night_blue_shift * 0.4);",
	"  float g_tint = 0.4 * (1.0 - night_blue_shift * 0.2);",
	"  float b_tint = 0.6 + night_blue_shift * 0.3;",
	"  vec3 night = vec3(luma * r_tint, luma * g_tint, luma * b_tint);",
	"  vec3 result = mix(c, night, night_amount);",
	"  frag_colour = vec4(result, src_alpha);",
	"}",
};

/* ---- Film grain ---- */

/** Fragment shader for film grain overlay. */
static const char *_frag_shader_pp_grain[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float grain_strength;",
	"uniform float time;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"",
	"/* Simple pseudo-random hash for grain generation. */",
	"float GrainHash(vec2 p) {",
	"  vec3 p3 = fract(vec3(p.xyx) * 0.1031);",
	"  p3 += dot(p3, p3.yzx + 33.33);",
	"  return fract((p3.x + p3.y) * p3.z);",
	"}",
	"",
	"void main() {",
	"  vec4 center = texture(source_tex, tex_coord);",
	"  vec3 c = center.rgb;",
	"  float src_alpha = center.a;",
	"  float noise = GrainHash(tex_coord * 1000.0 + time) * 2.0 - 1.0;",
	"  c += noise * grain_strength;",
	"  frag_colour = vec4(clamp(c, 0.0, 1.0), src_alpha);",
	"}",
};

/* ---- Bicubic (Catmull-Rom) texture filter ---- */

/**
 * Fragment shader implementing Catmull-Rom bicubic filtering.
 *
 * Uses 9 bilinear-hardware taps (instead of 16 point samples) by combining
 * adjacent weight pairs and letting GL_LINEAR interpolate between them.
 * This provides smoother upscaling than bilinear at a modest cost.
 */
static const char *_frag_shader_pp_bicubic[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec2 pixel = tex_coord / texel_size - 0.5;",
	"  vec2 f = fract(pixel);",
	"  vec2 pixel_center = (pixel - f + 0.5) * texel_size;",
	"",
	"  /* Catmull-Rom spline weights. */",
	"  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));",
	"  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);",
	"  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));",
	"  vec2 w3 = f * f * (-0.5 + 0.5 * f);",
	"",
	"  /* Combine w1+w2 for bilinear filtering trick (9 taps instead of 16). */",
	"  vec2 s12 = w1 + w2;",
	"  vec2 f12 = w2 / s12;",
	"",
	"  vec2 tc0 = pixel_center + (f12 - 1.0) * texel_size;",
	"  vec2 tc3 = pixel_center + (f12 + 1.0) * texel_size;",
	"  vec2 tc12 = pixel_center + f12 * texel_size;",
	"",
	"  /* 9-tap Catmull-Rom bicubic sampling. */",
	"  vec4 result = vec4(0.0);",
	"  result += texture(source_tex, vec2(tc0.x,  tc0.y))  * w0.x  * w0.y;",
	"  result += texture(source_tex, vec2(tc12.x, tc0.y))  * s12.x * w0.y;",
	"  result += texture(source_tex, vec2(tc3.x,  tc0.y))  * w3.x  * w0.y;",
	"  result += texture(source_tex, vec2(tc0.x,  tc12.y)) * w0.x  * s12.y;",
	"  result += texture(source_tex, vec2(tc12.x, tc12.y)) * s12.x * s12.y;",
	"  result += texture(source_tex, vec2(tc3.x,  tc12.y)) * w3.x  * s12.y;",
	"  result += texture(source_tex, vec2(tc0.x,  tc3.y))  * w0.x  * w3.y;",
	"  result += texture(source_tex, vec2(tc12.x, tc3.y))  * s12.x * w3.y;",
	"  result += texture(source_tex, vec2(tc3.x,  tc3.y))  * w3.x  * w3.y;",
	"",
	"  frag_colour = result;",
	"}",
};

/* ---- CRT scanline effect ---- */

/** Fragment shader for CRT scanline and curvature effect. */
static const char *_frag_shader_pp_crt[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"uniform vec2 screen_size;",
	"uniform float scanline_strength;",
	"uniform float curvature;",
	"uniform float chromatic_aberr;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  /* Apply barrel distortion for CRT curvature. */",
	"  vec2 uv = tex_coord - 0.5;",
	"  float r2 = dot(uv, uv);",
	"  uv *= 1.0 + curvature * r2;",
	"  uv += 0.5;",
	"",
	"  /* Discard pixels outside the curved screen area. */",
	"  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {",
	"    frag_colour = vec4(0.0, 0.0, 0.0, 1.0);",
	"    return;",
	"  }",
	"",
	"  /* Chromatic aberration: offset R and B channels horizontally. */",
	"  float ca = chromatic_aberr * texel_size.x;",
	"  float r = texture(source_tex, vec2(uv.x + ca, uv.y)).r;",
	"  vec4 center = texture(source_tex, uv);",
	"  float g = center.g;",
	"  float b = texture(source_tex, vec2(uv.x - ca, uv.y)).b;",
	"  vec3 c = vec3(r, g, b);",
	"  float src_alpha = center.a;",
	"",
	"  /* Darken every other scanline. */",
	"  float scanline = sin(uv.y * screen_size.y * 3.14159) * 0.5 + 0.5;",
	"  c *= 1.0 - scanline_strength * (1.0 - scanline);",
	"",
	"  frag_colour = vec4(c, src_alpha);",
	"}",
};

/* ---- Motion vector rasterization compute shader (GL 4.3+) ---- */

/** Compute shader: rasterize per-pixel MV + depth from draw command list.
 * Tile-based: each 16x16 workgroup processes only commands overlapping its tile. */
static const char *_compute_shader_mv_rasterize[] = {
	"#version 430\n",
	"struct DrawCommand { int sx, sy, w, h, mx, my, depth, pad; };",
	"layout(std430, binding=0) readonly buffer CmdBuf { DrawCommand cmds[]; };",
	"layout(std430, binding=1) readonly buffer TileBuf { int tile_data[]; };",
	"layout(rg16f, binding=0) writeonly uniform image2D mv_out;",
	"layout(r16f, binding=1) writeonly uniform image2D depth_out;",
	"uniform ivec2 screen_size;",
	"uniform ivec2 tile_count;",
	"uniform ivec2 global_motion;",
	"uniform int max_cmds_per_tile;",
	"layout(local_size_x=16, local_size_y=16) in;",
	"void main() {",
	"  ivec2 px = ivec2(gl_GlobalInvocationID.xy);",
	"  if (px.x >= screen_size.x || px.y >= screen_size.y) return;",
	"  ivec2 tile = px / 16;",
	"  int ti = tile.y * tile_count.x + tile.x;",
	"  int off = ti * (max_cmds_per_tile + 1);",
	"  int cnt = tile_data[off];",
	"  vec2 bm = vec2(global_motion) / 8.0;",
	"  float bd = 0.0;",
	"  for (int i = 0; i < cnt; i++) {",
	"    int ci = tile_data[off + 1 + i];",
	"    DrawCommand c = cmds[ci];",
	"    if (px.x >= c.sx && px.x < c.sx + c.w && px.y >= c.sy && px.y < c.sy + c.h) {",
	"      float d = float(c.depth) / 65535.0;",
	"      if (d >= bd) { bm = vec2(c.mx, c.my) / 8.0; bd = d; }",
	"    }",
	"  }",
	"  imageStore(mv_out, px, vec4(bm / vec2(screen_size), 0, 0));",
	"  imageStore(depth_out, px, vec4(bd, 0, 0, 0));",
	"}",
};

/* ---- Dynamic Lighting (Time-of-Day) ---- */

/** Fragment shader for time-of-day lighting. Shifts color temperature and brightness
 *  based on a 0-1 time value (0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk). */
static const char *_frag_shader_pp_lighting[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float time_of_day;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec4 src = texture(source_tex, tex_coord);",
	"  /* Brightness cycle: peaks at noon (0.5), darkest at midnight (0.0/1.0). */",
	"  float sun = clamp(sin(time_of_day * 6.28318530) * 0.5 + 0.5, 0.0, 1.0);",
	"  float brightness = 0.45 + sun * 0.55;",
	"  /* Color temperature: warm yellow at noon, cool blue at night, orange at dawn/dusk. */",
	"  float dawn_dusk = pow(sin(time_of_day * 6.28318530 * 2.0) * 0.5 + 0.5, 3.0);",
	"  vec3 noon_light  = vec3(1.00, 0.97, 0.90);",
	"  vec3 night_light = vec3(0.60, 0.65, 0.85);",
	"  vec3 dawn_light  = vec3(1.00, 0.80, 0.55);",
	"  vec3 light_color = mix(night_light, noon_light, sun);",
	"  light_color = mix(light_color, dawn_light, dawn_dusk * 0.6);",
	"  vec3 result = src.rgb * light_color * brightness;",
	"  frag_colour = vec4(clamp(result, 0.0, 1.0), src.a);",
	"}",
};

/* ---- Bloom (Threshold Pass) ---- */

/** Fragment shader for bloom threshold extraction. Outputs only pixels above luminance threshold. */
static const char *_frag_shader_pp_bloom_threshold[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float bloom_threshold;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  vec3 c = texture(source_tex, tex_coord).rgb;",
	"  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));",
	"  float contrib = max(0.0, luma - bloom_threshold) / max(1.0 - bloom_threshold, 0.001);",
	"  frag_colour = vec4(c * contrib, 1.0);",
	"}",
};

/** Fragment shader for bloom horizontal Gaussian blur (7-tap). */
static const char *_frag_shader_pp_bloom_blur_h[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  float w[4] = float[](0.3829, 0.2417, 0.0606, 0.0060);",
	"  vec3 sum = texture(source_tex, tex_coord).rgb * w[0];",
	"  for (int i = 1; i < 4; i++) {",
	"    vec2 off = vec2(float(i) * texel_size.x * 1.5, 0.0);",
	"    sum += texture(source_tex, tex_coord + off).rgb * w[i];",
	"    sum += texture(source_tex, tex_coord - off).rgb * w[i];",
	"  }",
	"  frag_colour = vec4(sum, 1.0);",
	"}",
};

/** Fragment shader for bloom vertical Gaussian blur (7-tap). */
static const char *_frag_shader_pp_bloom_blur_v[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform vec2 texel_size;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"void main() {",
	"  float w[4] = float[](0.3829, 0.2417, 0.0606, 0.0060);",
	"  vec3 sum = texture(source_tex, tex_coord).rgb * w[0];",
	"  for (int i = 1; i < 4; i++) {",
	"    vec2 off = vec2(0.0, float(i) * texel_size.y * 1.5);",
	"    sum += texture(source_tex, tex_coord + off).rgb * w[i];",
	"    sum += texture(source_tex, tex_coord - off).rgb * w[i];",
	"  }",
	"  frag_colour = vec4(sum, 1.0);",
	"}",
};

/* ---- Weather Effects (Rain / Snow Overlay) ---- */

/** Fragment shader for procedural weather particle overlay. */
static const char *_frag_shader_pp_weather[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform float time;",
	"uniform float weather_intensity;",
	"uniform float weather_type;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"",
	"float WeatherHash(vec2 p) {",
	"  vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));",
	"  p3 += dot(p3, p3.yzx + 33.33);",
	"  return fract((p3.x + p3.y) * p3.z);",
	"}",
	"",
	"void main() {",
	"  vec4 src = texture(source_tex, tex_coord);",
	"  float particle = 0.0;",
	"  /* Rain: thin vertical streaks, fast fall speed. */",
	"  if (weather_type > 0.5 && weather_type < 1.5) {",
	"    vec2 uv = tex_coord * vec2(120.0, 40.0);",
	"    uv.y -= time * 8.0;",
	"    vec2 cell = floor(uv);",
	"    vec2 f = fract(uv);",
	"    float h = WeatherHash(cell);",
	"    float streak = step(0.92, h) * step(abs(f.x - 0.5), 0.08) * smoothstep(0.0, 0.3, f.y) * smoothstep(1.0, 0.5, f.y);",
	"    particle = streak * 0.6;",
	"  }",
	"  /* Snow: larger slower diagonal drift. */",
	"  if (weather_type > 1.5) {",
	"    vec2 uv = tex_coord * vec2(60.0, 30.0);",
	"    uv.y -= time * 2.0;",
	"    uv.x += sin(time * 0.7 + tex_coord.y * 5.0) * 0.5;",
	"    vec2 cell = floor(uv);",
	"    vec2 f = fract(uv) - 0.5;",
	"    float h = WeatherHash(cell);",
	"    float flake = step(0.85, h) * (1.0 - smoothstep(0.0, 0.15, length(f)));",
	"    particle = flake * 0.8;",
	"  }",
	"  vec3 result = src.rgb + vec3(particle * weather_intensity);",
	"  frag_colour = vec4(clamp(result, 0.0, 1.0), src.a);",
	"}",
};

/* ---- Temporal accumulation (TAA-style upscaling prototype) ---- */

/**
 * Fragment shader for temporal accumulation upscaling.
 * Reprojects the previous frame using motion vectors and blends with
 * the current frame. This is a simplified version of the core technique
 * used by FSR 2 and DLSS, serving as a quality prototype (Gate 2).
 *
 * Inputs:
 *   source_tex: current frame color (render resolution, jittered)
 *   history_tex: previous frame accumulated output (display resolution)
 *   mv_tex: per-pixel motion vectors (RG16F, normalized screen-space)
 *   texel_size: 1.0 / display_size
 *   jitter_offset: current frame jitter in pixels
 *   reset: 1.0 to force history reset (scene cut)
 */
static const char *_frag_shader_pp_temporal_accum[] = {
	"#version 150\n",
	"uniform sampler2D source_tex;",
	"uniform sampler2D history_tex;",
	"uniform sampler2D mv_tex;",
	"uniform vec2 texel_size;",
	"uniform vec2 jitter_offset;",
	"uniform float reset;",
	"in vec2 tex_coord;",
	"out vec4 frag_colour;",
	"",
	"float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }",
	"",
	"void main() {",
	"  /* Read current frame color (unjitter by subtracting jitter offset). */",
	"  vec2 src_uv = tex_coord - jitter_offset * texel_size;",
	"  vec4 current = texture(source_tex, src_uv);",
	"",
	"  /* If reset is requested, output current frame directly. */",
	"  if (reset > 0.5) { frag_colour = current; return; }",
	"",
	"  /* Read motion vector and reproject to find history sample location. */",
	"  vec2 mv = texture(mv_tex, tex_coord).rg;",
	"  vec2 history_uv = tex_coord + mv;",
	"",
	"  /* Reject history if reprojected UV is out of bounds. */",
	"  if (history_uv.x < 0.0 || history_uv.x > 1.0 || history_uv.y < 0.0 || history_uv.y > 1.0) {",
	"    frag_colour = current;",
	"    return;",
	"  }",
	"",
	"  vec4 history = texture(history_tex, history_uv);",
	"",
	"  /* Neighborhood clamping: clamp history to the color range of current 3x3 neighborhood. */",
	"  /* This prevents ghosting when the scene changes in ways not captured by motion vectors. */",
	"  vec3 nmin = current.rgb;",
	"  vec3 nmax = current.rgb;",
	"  for (int y = -1; y <= 1; y++) {",
	"    for (int x = -1; x <= 1; x++) {",
	"      if (x == 0 && y == 0) continue;",
	"      vec3 n = texture(source_tex, src_uv + vec2(x, y) * texel_size).rgb;",
	"      nmin = min(nmin, n);",
	"      nmax = max(nmax, n);",
	"    }",
	"  }",
	"  history.rgb = clamp(history.rgb, nmin, nmax);",
	"",
	"  /* Blend: use higher weight for history (temporal stability) vs current (sharpness). */",
	"  /* Typical TAA uses 0.9-0.95 history weight. For upscaling, use less history */",
	"  /* to preserve current-frame detail at the cost of some temporal noise. */",
	"  float blend = 0.85;",
	"",
	"  /* Reduce history weight when motion is large (moving objects). */",
	"  float mv_magnitude = length(mv) * max(1.0 / texel_size.x, 1.0 / texel_size.y);",
	"  blend *= 1.0 - clamp(mv_magnitude * 0.1, 0.0, 0.5);",
	"",
	"  frag_colour = vec4(mix(current.rgb, history.rgb, blend), current.a);",
	"}",
};
