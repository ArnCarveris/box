#include "include/fullscreen.wgsl"
#include "include/fx_params.wgsl"
#include "include/bloom.wgsl"

@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var input_texture: texture_2d<f32>;
@group(0) @binding(2) var bloom_sampler: sampler;

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

fn threshold_precomputations() -> vec4<f32> {
  let threshold = max(fx.params[1].x, 0.0);
  let knee = threshold * clamp(fx.params[1].y, 0.0, 1.0);
  return vec4(threshold, threshold - knee, 2.0 * knee, 0.25 / (knee + 0.00001));
}

fn soft_threshold(color : vec3<f32>) -> vec3<f32> {
  let brightness = max(color.r, max(color.g, color.b));
  var softness = brightness - threshold_precomputations().y;
  softness = clamp(softness, 0.0, threshold_precomputations().z);
  softness = softness * softness * threshold_precomputations().w;
  var contribution = max(brightness - threshold_precomputations().x, softness);
  contribution /= max(brightness, 0.00001);
  return color * contribution;
}
fn tonemapping_luminance(v : vec3<f32>) -> f32 {
  return dot(v, vec3<f32>(0.2126, 0.7152, 0.0722));
}
fn karis_average(color : vec3<f32>) -> f32 {
  let luma = tonemapping_luminance(color) / 4.0;
  return mix(1.0, 1.0 / (1.0 + luma), clamp(fx.params[2].y, 0.0, 1.0));
}
fn suppress_firefly(encoded : vec3<f32>) -> vec3<f32> {
  let color = fx_decode(encoded, fx.frame.z);
  if (fx.params[1].w <= 0.0) {
    return color;
  }
  let luma = max(tonemapping_luminance(color), 0.00001);
  return color * min(1.0, fx.params[1].w / luma);
}
fn tap(uv : vec2<f32>, offset : vec2<f32>, step : vec2<f32>) -> vec3<f32> {
  return textureSampleLevel(input_texture, bloom_sampler, uv + offset * step, 0.0).rgb;
}
fn sample_13_tap_first(uv : vec2<f32>) -> vec3<f32> {
  let s = fx.params[3].xy;
  let a = suppress_firefly(tap(uv, vec2<f32>(-2.0,  2.0), s));
  let b = suppress_firefly(tap(uv, vec2<f32>( 0.0,  2.0), s));
  let c = suppress_firefly(tap(uv, vec2<f32>( 2.0,  2.0), s));
  let d = suppress_firefly(tap(uv, vec2<f32>(-2.0,  0.0), s));
  let e = suppress_firefly(tap(uv, vec2<f32>( 0.0,  0.0), s));
  let f = suppress_firefly(tap(uv, vec2<f32>( 2.0,  0.0), s));
  let g = suppress_firefly(tap(uv, vec2<f32>(-2.0, -2.0), s));
  let h = suppress_firefly(tap(uv, vec2<f32>( 0.0, -2.0), s));
  let i = suppress_firefly(tap(uv, vec2<f32>( 2.0, -2.0), s));
  let j = suppress_firefly(tap(uv, vec2<f32>(-1.0,  1.0), s));
  let k = suppress_firefly(tap(uv, vec2<f32>( 1.0,  1.0), s));
  let l = suppress_firefly(tap(uv, vec2<f32>(-1.0, -1.0), s));
  let m = suppress_firefly(tap(uv, vec2<f32>( 1.0, -1.0), s));
  var group0 = (a + b + d + e) * (0.125 / 4.0);
  var group1 = (b + c + e + f) * (0.125 / 4.0);
  var group2 = (d + e + g + h) * (0.125 / 4.0);
  var group3 = (e + f + h + i) * (0.125 / 4.0);
  var group4 = (j + k + l + m) * (0.5 / 4.0);
  group0 *= karis_average(group0);
  group1 *= karis_average(group1);
  group2 *= karis_average(group2);
  group3 *= karis_average(group3);
  group4 *= karis_average(group4);
  return group0 + group1 + group2 + group3 + group4;
}
fn sample_13_tap(uv : vec2<f32>) -> vec3<f32> {
  let a = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-2,  2)).rgb;
  let b = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 0,  2)).rgb;
  let c = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 2,  2)).rgb;
  let d = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-2,  0)).rgb;
  let e = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0).rgb;
  let f = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 2,  0)).rgb;
  let g = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-2, -2)).rgb;
  let h = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 0, -2)).rgb;
  let i = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 2, -2)).rgb;
  let j = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-1,  1)).rgb;
  let k = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 1,  1)).rgb;
  let l = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>(-1, -1)).rgb;
  let m = textureSampleLevel(input_texture, bloom_sampler, uv, 0.0, vec2<i32>( 1, -1)).rgb;
  var sample = (a + c + g + i) * 0.03125;
  sample += (b + d + f + h) * 0.0625;
  sample += (e + j + k + l + m) * 0.125;
  return sample;
}
fn prefiltered(uv : vec2<f32>) -> vec4<f32> {
  var sample = sample_13_tap_first(uv);
  sample = clamp(sample, vec3<f32>(0.0001), vec3<f32>(3.40282347e+37));
  if (threshold_precomputations().x > 0.0 || threshold_precomputations().z > 0.0) {
    sample = soft_threshold(sample);
  }
  return vec4<f32>(sample, 1.0);
}
fn resolved(uv : vec2<f32>) -> vec4<f32> {
  let ts = 1.0 / vec2<f32>(textureDimensions(input_texture, 0));
  let c = tap(uv, vec2<f32>( 0.0,  0.0), ts);
  let n0 = tap(uv, vec2<f32>(-1.0,  1.0), ts);
  let n1 = tap(uv, vec2<f32>( 0.0,  1.0), ts);
  let n2 = tap(uv, vec2<f32>( 1.0,  1.0), ts);
  let n3 = tap(uv, vec2<f32>(-1.0,  0.0), ts);
  let n4 = tap(uv, vec2<f32>( 1.0,  0.0), ts);
  let n5 = tap(uv, vec2<f32>(-1.0, -1.0), ts);
  let n6 = tap(uv, vec2<f32>( 0.0, -1.0), ts);
  let n7 = tap(uv, vec2<f32>( 1.0, -1.0), ts);
  let lo = min(min(min(min(c, n0), min(n1, n2)), min(min(n3, n4), min(n5, n6))), n7);
  let hi = max(max(max(max(c, n0), max(n1, n2)), max(max(n3, n4), max(n5, n6))), n7);
  let hist = textureSampleLevel(scene_texture, bloom_sampler, uv, 0.0).rgb;
  let clamped = clamp(hist, lo, hi);
  return vec4<f32>(mix(c, clamped, clamp(fx.params[2].x, 0.0, 0.98)), 1.0);
}
@fragment fn fs_first(@location(0) output_uv : vec2<f32>) -> @location(0) vec4<f32> {
  return prefiltered(output_uv);
}
@fragment fn fs_down(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(sample_13_tap(uv), 1.0);
}
@fragment fn fs_up(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(bloom_tent(input_texture, bloom_sampler, uv), 1.0);
}
@group(0) @binding(3) var scene_texture : texture_2d<f32>;
@fragment fn fs_history(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {
  return resolved(uv);
}

struct Level {
  weight: vec4<f32>,
};

@group(0) @binding(4) var level_texture : texture_storage_2d<rgba16float, write>;
@group(0) @binding(5) var<uniform> level : Level;

fn level_uv(id : vec2<u32>) -> vec2<f32> {
  return (vec2<f32>(id) + vec2<f32>(0.5)) / vec2<f32>(textureDimensions(level_texture));
}
fn level_outside(id : vec2<u32>) -> bool {
  let size = textureDimensions(level_texture);
  return id.x >= size.x || id.y >= size.y;
}
@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_first(@builtin(global_invocation_id) id : vec3<u32>) {
  if (level_outside(id.xy)) {
    return;
  }
  textureStore(level_texture, vec2<i32>(id.xy), prefiltered(level_uv(id.xy)));
}
@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_down(@builtin(global_invocation_id) id : vec3<u32>) {
  if (level_outside(id.xy)) {
    return;
  }
  textureStore(level_texture, vec2<i32>(id.xy), vec4<f32>(sample_13_tap(level_uv(id.xy)), 1.0));
}
@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_up(@builtin(global_invocation_id) id : vec3<u32>) {
  if (level_outside(id.xy)) {
    return;
  }
  let base = textureLoad(scene_texture, vec2<i32>(id.xy), 0);
  let tent = bloom_tent(input_texture, bloom_sampler, level_uv(id.xy));
  textureStore(level_texture, vec2<i32>(id.xy), vec4<f32>(base.rgb + tent * level.weight.x, base.a));
}
@compute @workgroup_size(MIP_GROUP, MIP_GROUP)
fn cs_history(@builtin(global_invocation_id) id : vec3<u32>) {
  if (level_outside(id.xy)) {
    return;
  }
  textureStore(level_texture, vec2<i32>(id.xy), resolved(level_uv(id.xy)));
}
