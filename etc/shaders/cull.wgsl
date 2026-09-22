struct Cull {
  planes: array<vec4<f32>, FRUSTUM_PLANES>,
  first: u32,
  count: u32,
  base: u32,
  arg: u32,
  caster: u32,
  pad0: u32,
  pad1: u32,
  pad2: u32,
  clip: mat4x4<f32>,
  pixels: vec4<f32>,
};

@group(0) @binding(0) var<uniform> c: Cull;
@group(0) @binding(1) var<storage, read> src: array<u32>;
@group(0) @binding(2) var<storage, read_write> dst: array<u32>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>>;

const WORDS: u32 = u32(INSTANCE_WORDS);
const BOUNDS: u32 = u32(INSTANCE_BOUNDS);
const MODE: u32 = u32(INSTANCE_MODE);
const NOCAST: u32 = 1u << u32(MODE_NOCAST_SHIFT);
const ARGS: u32 = u32(CULL_ARG_WORDS);

#include "include/screen_size.wgsl"

fn visible(center: vec3<f32>, radius: f32) -> bool {
  var result = true;
  for (var i = 0u; i < u32(FRUSTUM_PLANES); i = i + 1u) {
    let p = c.planes[i];
    if (dot(p.xyz, center) + p.w < -radius) {
      result = false;
    }
  }
  return result;
}

@compute @workgroup_size(CULL_GROUP)
fn cs_main(@builtin(global_invocation_id) id: vec3<u32>) {
  if (id.x >= c.count) {
    return;
  }
  let base = (c.first + id.x) * WORDS;
  if (c.caster != 0u && (src[base + MODE] & NOCAST) != 0u) {
    return;
  }
  let center = vec3<f32>(bitcast<f32>(src[base + BOUNDS]),
    bitcast<f32>(src[base + BOUNDS + 1u]), bitcast<f32>(src[base + BOUNDS + 2u]));
  if (!visible(center, bitcast<f32>(src[base + BOUNDS + 3u]))) {
    return;
  }
  if (!large_enough(base, center, c.clip, c.pixels)) {
    return;
  }
  let index = atomicAdd(&args[c.arg * ARGS + 1u], 1u);
  let out = (c.base + index) * WORDS;
  for (var i = 0u; i < WORDS; i = i + 1u) {
    dst[out + i] = src[base + i];
  }
}
