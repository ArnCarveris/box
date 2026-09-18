#pragma once

struct Hiz {
  clip: mat4x4<f32>,
  prev: mat4x4<f32>,
  planes: array<vec4<f32>, FRUSTUM_PLANES>,
  range: vec4<u32>,
  screen: vec4<u32>,
};

@group(0) @binding(0) var<uniform> c: Hiz;
@group(0) @binding(1) var<storage, read> src: array<u32>;
@group(0) @binding(2) var<storage, read_write> dst: array<u32>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> counters: array<atomic<u32>>;
@group(0) @binding(5) var pyramid: texture_2d<f32>;
@group(0) @binding(6) var history: texture_2d<f32>;

const WORDS: u32 = u32(INSTANCE_WORDS);
const BOUNDS: u32 = u32(INSTANCE_BOUNDS);
const ARGS: u32 = u32(CULL_ARG_WORDS);

struct Rect {
  lo: vec2<f32>,
  hi: vec2<f32>,
  depth: f32,
  clamped: bool,
};

fn in_frustum(center: vec3<f32>, radius: f32) -> bool {
  var result = true;
  for (var i = 0u; i < u32(FRUSTUM_PLANES); i = i + 1u) {
    let p = c.planes[i];
    if (dot(p.xyz, center) + p.w < -radius) {
      result = false;
    }
  }
  return result;
}

fn project(m: mat4x4<f32>, center: vec3<f32>, radius: f32) -> Rect {
  var lo = vec2(1.0, 1.0);
  var hi = vec2(0.0, 0.0);
  var depth = 0.0;
  var crossed = false;
  for (var i = 0u; i < 8u; i = i + 1u) {
    let corner = vec3(f32(i & 1u), f32((i >> 1u) & 1u), f32((i >> 2u) & 1u));
    let clip = m * vec4(center + (corner * 2.0 - 1.0) * radius, 1.0);
    if (clip.w <= 1e-6) {
      crossed = true;
    } else {
      let ndc = clip.xyz / clip.w;
      let uv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
      lo = min(lo, uv);
      hi = max(hi, uv);
      depth = max(depth, ndc.z);
    }
  }
  if (crossed) {
    lo = vec2(0.0, 0.0);
    hi = vec2(1.0, 1.0);
    depth = 1.0;
  }
  return Rect(clamp(lo, vec2(0.0), vec2(1.0)), clamp(hi, vec2(0.0), vec2(1.0)),
    min(depth, 1.0), crossed);
}

fn level_of(r: Rect) -> u32 {
  let span = (r.hi - r.lo) * vec2(f32(c.screen.x), f32(c.screen.y));
  var extent = max(span.x, span.y);
  var result = 0u;
  loop {
    if (extent <= 1.0 || result + 1u >= c.screen.z) {
      break;
    }
    extent = extent * 0.5;
    result = result + 1u;
  }
  return result;
}

fn texels(r: Rect, level: u32) -> vec4<u32> {
  let size = max(c.screen.xy >> vec2(level, level), vec2(1u, 1u));
  let limit = size - vec2(1u, 1u);
  let a = min(vec2<u32>(r.lo * vec2<f32>(size)), limit);
  let b = min(vec2<u32>(r.hi * vec2<f32>(size)), limit);
  return vec4(a.x, a.y, b.x, b.y);
}

fn shown_now(center: vec3<f32>, radius: f32) -> bool {
  let r = project(c.clip, center, radius);
  var result = true;
  if (!r.clamped) {
    let level = level_of(r);
    let t = texels(r, level);
    var far = textureLoad(pyramid, vec2(t.x, t.y), level).r;
    far = min(far, textureLoad(pyramid, vec2(t.z, t.y), level).r);
    far = min(far, textureLoad(pyramid, vec2(t.x, t.w), level).r);
    far = min(far, textureLoad(pyramid, vec2(t.z, t.w), level).r);
    result = r.depth >= far;
  }
  return result;
}

fn shown_before(center: vec3<f32>, radius: f32) -> bool {
  let r = project(c.prev, center, radius);
  var result = true;
  if (!r.clamped) {
    let level = level_of(r);
    let t = texels(r, level);
    var far = textureLoad(history, vec2(t.x, t.y), level).r;
    far = min(far, textureLoad(history, vec2(t.z, t.y), level).r);
    far = min(far, textureLoad(history, vec2(t.x, t.w), level).r);
    far = min(far, textureLoad(history, vec2(t.z, t.w), level).r);
    result = r.depth >= far;
  }
  return result;
}

fn append(base: u32) {
  let index = atomicAdd(&args[c.range.w * ARGS + 1u], 1u);
  let out = (c.range.z + index) * WORDS;
  for (var i = 0u; i < WORDS; i = i + 1u) {
    dst[out + i] = src[base + i];
  }
}
