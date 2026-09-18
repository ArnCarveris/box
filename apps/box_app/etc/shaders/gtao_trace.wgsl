#include "include/fullscreen.wgsl"
#include "include/fx_params.wgsl"
#include "include/octahedral.wgsl"

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

// Fraction of the view distance the denoise kernel reaches over, which is wider
// than the history test because a surface seen at a grazing angle recedes fast.
const AO_SPREAD: f32 = 0.25;

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

fn ao_motion(uv: vec2<f32>) -> vec2<f32> {
  let size = vec2<i32>(textureDimensions(ao_motion_tex, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(ao_motion_tex, c, 0).xy;
}

// Interleaved gradient noise, which spreads the step offsets of neighbouring
// pixels so the denoise has something to average.
fn ao_noise(p: vec2<f32>) -> f32 {
  return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Turn the slices of a pixel are rotated by, an ordered four by four pattern.
// The denoise below averages exactly one four by four block, so every one of
// the sixteen directions reaches it exactly once and the pattern cancels.
fn ao_turn(p: vec2<u32>) -> f32 {
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

  let delta = ao_world(uv, d) - p;
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
  let p = ao_world(uv, depth);
  let to_eye = eye - p;
  let dist = max(length(to_eye), 0.00001);
  let v = to_eye / dist;
  let n = ao_normal(uv);
  let radius = max(fx.params[0].x, 0.01);
  let slices = max(u32(fx.params[0].y), 1u);
  let steps = max(u32(fx.params[0].z), 1u);

  let texel = vec2(1.0) / max(fx.size.xy, vec2(1.0));
  let side = ao_world(uv + vec2(texel.x, 0.0), depth) - p;
  let per_uv = length(side) / texel.x;
  let uv_radius = clamp(radius / max(per_uv, 0.0001), texel.x, 0.25);
  var turn = ao_turn(vec2<u32>(in.pos.xy));
  if (fx.params[1].x > 0.5) {
    turn = fract(turn + fract(fx.frame.y * 60.0 * 0.6180339887));
  }

  let jitter = ao_noise(in.pos.xy);

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

@fragment fn fs_resolve(in: AoOut) -> @location(0) vec4<f32> {
  let size = vec2<i32>(textureDimensions(ao_raw_tex, 0));
  let c = clamp(vec2<i32>(in.pos.xy), vec2(0, 0), size - vec2(1, 1));
  let dist = textureLoad(ao_raw_tex, c, 0).y;
  var sum = 0.0;
  var weight = 0.0;
  for (var y = -2; y <= 1; y = y + 1) {
    for (var x = -2; x <= 1; x = x + 1) {
      let s = clamp(c + vec2(x, y), vec2(0, 0), size - vec2(1, 1));
      let tap = textureLoad(ao_raw_tex, s, 0);
      var w = 1.0;
      if (dist > 0.0) {
        let delta = abs(tap.y - dist) / max(dist * AO_SPREAD, 0.05);
        w = 1.0 / (1.0 + delta * delta);
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
