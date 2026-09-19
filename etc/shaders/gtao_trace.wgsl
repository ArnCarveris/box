#include "include/fullscreen.wgsl"
#include "include/fx_params.wgsl"
#include "include/gbuffer.wgsl"

// Ground truth ambient occlusion of a view, in two passes the frame runs for
// the effect that asked for the occlusion input. The trace walks a few slices
// of the hemisphere of a pixel, finds the highest occluder on each side of a
// slice in the depth target and integrates the visibility between the two
// horizons; the resolve denoises that with a depth aware kernel and mixes it
// with the occlusion of the previous frame, reprojected with the motion vectors.
@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var ao_depth_tex: texture_depth_2d;
@group(0) @binding(2) var ao_normal_tex: texture_2d<f32>;
@group(0) @binding(3) var ao_samp: sampler;
@group(0) @binding(4) var ao_motion_tex: texture_2d<f32>;
@group(0) @binding(5) var ao_raw_tex: texture_2d<f32>;
@group(0) @binding(6) var ao_hist_tex: texture_2d<f32>;

// Quarter turn, the widest a horizon reaches from the normal.
const AO_HALF_PI: f32 = 1.5707963268;

// Weight the accumulated history keeps where a pixel is accepted.
const AO_HISTORY: f32 = 0.9;

// Fraction of the view distance two pixels may differ by and still be one surface.
const AO_TOLERANCE: f32 = 0.05;

// Fraction of the view distance a tap may sit off the tangent plane of the
// pixel and still be averaged into it by the denoise.
const AO_SPREAD: f32 = 0.02;

struct AoOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> AoOut {
  let corner = fullscreen_corner(index);
  var out: AoOut;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

fn ao_depth(uv: vec2<f32>) -> f32 {
  let size = vec2<i32>(textureDimensions(ao_depth_tex, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(ao_depth_tex, c, 0);
}

// Centre of the depth texel a coordinate falls in. The depth of a texel belongs
// to its centre, so a world position rebuilt at the coordinate that was asked
// for instead sits off the surface by up to half the slope of a texel, which on
// a floor seen at a grazing angle is metres and scatters the horizons of the
// trace into bands that follow the texel grid.
fn ao_snap(uv: vec2<f32>) -> vec2<f32> {
  let size = max(vec2<f32>(textureDimensions(ao_depth_tex, 0)), vec2(1.0));
  return (floor(uv * size) + vec2(0.5)) / size;
}

fn ao_world(uv: vec2<f32>, depth: f32) -> vec3<f32> {
  let ndc = vec3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, max(depth, 0.0000001));
  let p = fx.inv_clip * vec4(ndc, 1.0);
  return p.xyz / p.w;
}

// Direction the view ray of a pixel travels, which does not depend on a depth.
fn ao_ray(uv: vec2<f32>) -> vec3<f32> {
  return normalize(ao_world(uv, 0.5) - fx.eye.xyz);
}

fn ao_normal(uv: vec2<f32>) -> vec3<f32> {
  let size = vec2<i32>(textureDimensions(ao_normal_tex, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return oct_decode(textureLoad(ao_normal_tex, c, 0).xy);
}

// Resolution the trace and the resolve run at, which the history of the view
// has and the effect uniform, whose size is the target of the chain pass, does
// not. Every step and every kernel offset is measured in these texels.
fn ao_extent() -> vec2<f32> {
  return max(vec2<f32>(textureDimensions(ao_hist_tex, 0)), vec2(1.0));
}

fn ao_motion(uv: vec2<f32>) -> vec2<f32> {
  let size = vec2<i32>(textureDimensions(ao_motion_tex, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(ao_motion_tex, c, 0).xy;
}

// Ordered four by four pattern, the sixteen values of a block spread over zero
// to one. Both the slice rotation and the step offset of a pixel come from it,
// so the whole sampling pattern repeats every four texels and the denoise
// below, which averages exactly one four by four block, cancels it completely.
// A noise that does not repeat on that grid leaves structure behind instead.
fn ao_ordered(p: vec2<u32>) -> f32 {
  let x = p.x & 3u;
  let y = p.y & 3u;
  let a = x ^ y;
  var v = (y >> 1u) & 1u;
  v = v | (((a >> 1u) & 1u) << 1u);
  v = v | ((y & 1u) << 2u);
  v = v | ((a & 1u) << 3u);
  return f32(v) / 16.0;
}

fn ao_clamp_horizon(angle: f32, normal: f32) -> f32 {
  return clamp(angle, normal - AO_HALF_PI, normal + AO_HALF_PI);
}

// Cosine weighted visibility between the two horizons of a slice, the closed
// form the CPU twin in renderer/gtao.h checks.
fn ao_slice(normal: f32, h1: f32, h2: f32, proj_len: f32) -> f32 {
  let a = ao_clamp_horizon(h1, normal);
  let b = ao_clamp_horizon(h2, normal);
  let cn = cos(normal);
  let sn = sin(normal);
  let first = -cos(2.0 * a - normal) + cn + 2.0 * a * sn;
  let second = -cos(2.0 * b - normal) + cn + 2.0 * b * sn;
  return proj_len * 0.25 * (first + second);
}

// Highest occluder one sample of a slice sees, folded into the horizon so far.
fn ao_horizon(best: f32, uv: vec2<f32>, p: vec3<f32>, v: vec3<f32>, radius: f32) -> f32 {
  let d = ao_depth(uv);
  if (d <= 0.0) {
    return best;
  }

  let delta = ao_world(ao_snap(uv), d) - p;
  let len = length(delta);
  if (len < 0.00001) {
    return best;
  }

  let fade = clamp(1.0 - len / radius, 0.0, 1.0);
  return max(best, mix(-1.0, dot(delta / len, v), fade));
}

@fragment fn fs_trace(in: AoOut) -> @location(0) vec4<f32> {
  let uv = in.uv;
  let depth = ao_depth(uv);
  if (depth <= 0.0) {
    return vec4(1.0, 0.0, 0.0, 1.0);
  }

  let eye = fx.eye.xyz;
  let p = ao_world(ao_snap(uv), depth);
  let to_eye = eye - p;
  let dist = max(length(to_eye), 0.00001);
  let v = to_eye / dist;
  let n = ao_normal(uv);
  let radius = max(fx.params[0].x, 0.01);
  let slices = max(u32(fx.params[0].y), 1u);
  let steps = max(u32(fx.params[0].z), 1u);

  let texel = vec2(1.0) / ao_extent();
  let side = ao_world(ao_snap(uv) + vec2(texel.x, 0.0), depth) - p;
  let per_uv = length(side) / texel.x;
  let uv_radius = clamp(radius / max(per_uv, 0.0001), texel.x, 0.25);
  let pixel = vec2<u32>(in.pos.xy);
  var turn = ao_ordered(pixel);
  if (fx.params[1].x > 0.5) {
    turn = fract(turn + fract(fx.frame.y * 60.0 * 0.6180339887));
  }

  let jitter = ao_ordered(pixel + vec2(2u, 1u));

  var visibility = 0.0;
  for (var s = 0u; s < slices; s = s + 1u) {
    let phi = (f32(s) + turn) / f32(slices) * 3.14159265;
    let dir = vec2(cos(phi), sin(phi));
    let d0 = ao_ray(uv);
    let d1 = ao_ray(uv + dir * texel * 4.0);
    let along = d1 - d0 * dot(d0, d1);
    let along_len = length(along);
    if (along_len < 0.000001) {
      continue;
    }

    let t = along / along_len;
    let plane = normalize(cross(d0, d1));
    let projected = n - plane * dot(n, plane);
    let proj_len = length(projected);
    if (proj_len < 0.00001) {
      continue;
    }

    let pn = projected / proj_len;
    let normal_angle = atan2(dot(pn, t), dot(pn, v));
    var cos1 = -1.0;
    var cos2 = -1.0;
    for (var i = 0u; i < steps; i = i + 1u) {
      let step = (f32(i) + jitter) / f32(steps) * uv_radius;
      cos2 = ao_horizon(cos2, uv + dir * step, p, v, radius);
      cos1 = ao_horizon(cos1, uv - dir * step, p, v, radius);
    }

    let h2 = acos(clamp(cos2, -1.0, 1.0));
    let h1 = -acos(clamp(cos1, -1.0, 1.0));
    visibility = visibility + ao_slice(normal_angle, h1, h2, proj_len);
  }

  return vec4(clamp(visibility / f32(slices), 0.0, 1.0), dist, 0.0, 1.0);
}

// Denoise weight of one tap: how far the surface it saw sits off the tangent
// plane of the pixel. A tap on the same plane weighs one whatever the angle the
// surface is seen at, which is what lets the four by four block average evenly
// and cancel the ordered pattern of the trace; comparing view distances instead
// collapses the kernel on a floor seen at a grazing angle and leaves the
// pattern of the trace standing in the image.
fn ao_plane_weight(tap_uv: vec2<f32>, center: vec3<f32>, normal: vec3<f32>,
                   dist: f32) -> f32 {
  let depth = ao_depth(tap_uv);
  if (depth <= 0.0) {
    return 0.0001;
  }

  let off = abs(dot(ao_world(ao_snap(tap_uv), depth) - center, normal)) /
    max(dist * AO_SPREAD, 0.02);
  return 1.0 / (1.0 + off * off);
}

@fragment fn fs_resolve(in: AoOut) -> @location(0) vec4<f32> {
  let size = vec2<i32>(textureDimensions(ao_raw_tex, 0));
  let c = clamp(vec2<i32>(in.pos.xy), vec2(0, 0), size - vec2(1, 1));
  let texel = vec2(1.0) / ao_extent();
  let dist = textureLoad(ao_raw_tex, c, 0).y;
  let depth = ao_depth(in.uv);
  let center = ao_world(ao_snap(in.uv), max(depth, 0.0000001));
  let normal = ao_normal(in.uv);
  var sum = 0.0;
  var weight = 0.0;
  for (var y = -2; y <= 1; y = y + 1) {
    for (var x = -2; x <= 1; x = x + 1) {
      let s = clamp(c + vec2(x, y), vec2(0, 0), size - vec2(1, 1));
      let tap = textureLoad(ao_raw_tex, s, 0);
      var w = 1.0;
      if (dist > 0.0 && depth > 0.0) {
        w = ao_plane_weight(in.uv + vec2(f32(x), f32(y)) * texel, center, normal, dist);
      }
      sum = sum + tap.x * w;
      weight = weight + w;
    }
  }

  var ao = sum / max(weight, 0.00001);
  if (fx.params[1].x > 0.5 && dist > 0.0) {
    let prev = in.uv - ao_motion(in.uv);
    if (all(prev > vec2(0.0)) && all(prev < vec2(1.0))) {
      let history = textureSampleLevel(ao_hist_tex, ao_samp, prev, 0.0);
      if (history.y > 0.0 && abs(history.y - dist) < dist * AO_TOLERANCE + 0.05) {
        ao = mix(ao, history.x, AO_HISTORY);
      }
    }
  }

  return vec4(clamp(ao, 0.0, 1.0), dist, 0.0, 1.0);
}
