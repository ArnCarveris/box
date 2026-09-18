#include "include/fullscreen.wgsl"
#include "include/fx_params.wgsl"

// Progressive pyramid of the image an effect reads: a thirteen tap downsample
// down the levels and an eight tap tent upsample mixed back up them, the same
// filter pair the blurred background copy uses. The first downsample decodes
// the image, so every level holds linear light and a wide kernel costs a few
// small passes instead of a wide blur.
@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;

struct Out {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> Out {
  let corner = fullscreen_corner(index);
  var out: Out;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

fn tap(uv: vec2<f32>) -> vec3<f32> {
  return textureSampleLevel(src, samp, uv, 0.0).rgb;
}

// Firefly suppression: one very bright texel is clamped to the luminance the
// component allows and every group of the filter is weighted by the Karis
// average, so it cannot flicker its way through the pyramid.
fn tap_first(uv: vec2<f32>) -> vec3<f32> {
  let c = fx_decode(tap(uv), fx.frame.z);
  let clamp_luma = fx.params[0].w;
  let luma = fx_luma(c);
  if (clamp_luma > 0.0 && luma > clamp_luma) {
    return c * (clamp_luma / luma);
  }

  return c;
}

fn karis(c: vec3<f32>) -> f32 {
  return 1.0 / (1.0 + fx_luma(c));
}

// Soft knee: a quadratic rolloff around the knee luminance, which dims what is
// dark instead of cutting it off. A knee of zero lets every texel bloom.
fn knee(c: vec3<f32>) -> vec3<f32> {
  let k = fx.params[0].z;
  if (k <= 0.0) {
    return c;
  }

  let luma = fx_luma(c);
  return c * (luma * luma / (luma * luma + k * k));
}

@fragment fn fs_first(in: Out) -> @location(0) vec4<f32> {
  let ps = 1.0 / vec2<f32>(textureDimensions(src, 0));
  let pl = 2.0 * ps;
  let uv = in.uv;
  let a = tap_first(uv + vec2(-pl.x, pl.y));
  let b = tap_first(uv + vec2(0.0, pl.y));
  let c = tap_first(uv + pl);
  let d = tap_first(uv + vec2(-pl.x, 0.0));
  let e = tap_first(uv);
  let f = tap_first(uv + vec2(pl.x, 0.0));
  let g = tap_first(uv - pl);
  let h = tap_first(uv + vec2(0.0, -pl.y));
  let i = tap_first(uv + vec2(pl.x, -pl.y));
  let j = tap_first(uv + vec2(-ps.x, ps.y));
  let k = tap_first(uv + ps);
  let l = tap_first(uv - ps);
  let m = tap_first(uv + vec2(ps.x, -ps.y));
  let g0 = (j + k + l + m) * 0.25;
  let g1 = (a + b + d + e) * 0.25;
  let g2 = (b + c + e + f) * 0.25;
  let g3 = (d + e + g + h) * 0.25;
  let g4 = (e + f + h + i) * 0.25;
  let w0 = karis(g0) * 0.5;
  let w1 = karis(g1) * 0.125;
  let w2 = karis(g2) * 0.125;
  let w3 = karis(g3) * 0.125;
  let w4 = karis(g4) * 0.125;
  let sum = g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4;
  return vec4(knee(sum / max(w0 + w1 + w2 + w3 + w4, 0.0001)), 1.0);
}

@fragment fn fs_down(in: Out) -> @location(0) vec4<f32> {
  let ps = 1.0 / vec2<f32>(textureDimensions(src, 0));
  let pl = 2.0 * ps;
  let uv = in.uv;
  let a = tap(uv + vec2(-pl.x, pl.y));
  let b = tap(uv + vec2(0.0, pl.y));
  let c = tap(uv + pl);
  let d = tap(uv + vec2(-pl.x, 0.0));
  let e = tap(uv);
  let f = tap(uv + vec2(pl.x, 0.0));
  let g = tap(uv - pl);
  let h = tap(uv + vec2(0.0, -pl.y));
  let i = tap(uv + vec2(pl.x, -pl.y));
  let j = tap(uv + vec2(-ps.x, ps.y));
  let k = tap(uv + ps);
  let l = tap(uv - ps);
  let m = tap(uv + vec2(ps.x, -ps.y));
  var sum = (a + c + g + i) * 0.03125;
  sum = sum + (b + d + f + h) * 0.0625;
  sum = sum + (e + j + k + l + m) * 0.125;
  return vec4(sum, 1.0);
}

@fragment fn fs_up(in: Out) -> @location(0) vec4<f32> {
  let o = 0.5 / vec2<f32>(textureDimensions(src, 0));
  var sum = tap(in.uv + vec2(-o.x * 2.0, 0.0));
  sum = sum + tap(in.uv + vec2(-o.x, o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(0.0, o.y * 2.0));
  sum = sum + tap(in.uv + vec2(o.x, o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(o.x * 2.0, 0.0));
  sum = sum + tap(in.uv + vec2(o.x, -o.y)) * 2.0;
  sum = sum + tap(in.uv + vec2(0.0, -o.y * 2.0));
  sum = sum + tap(in.uv + vec2(-o.x, -o.y)) * 2.0;
  return vec4(sum / 12.0, 1.0);
}
