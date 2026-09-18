#pragma once

#include "fullscreen.wgsl"

struct Effect {
  params: array<vec4<f32>, EFFECT_PARAM_VECS>,
  size: vec4<f32>,
};

@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var fx_color: texture_2d<f32>;
@group(0) @binding(2) var fx_sampler: sampler;
@group(0) @binding(3) var fx_depth: texture_depth_2d;
@group(0) @binding(4) var fx_normal: texture_2d<f32>;
@group(0) @binding(5) var fx_motion: texture_2d<f32>;

struct FxOut {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) index: u32) -> FxOut {
  let corner = fullscreen_corner(index);
  var out: FxOut;
  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);
  out.uv = vec2(corner.x, 1.0 - corner.y);
  return out;
}

fn fx_sample(uv: vec2<f32>) -> vec4<f32> {
  return textureSampleLevel(fx_color, fx_sampler, uv, 0.0);
}

fn fx_world_normal(uv: vec2<f32>) -> vec3<f32> {
  let size = vec2<i32>(textureDimensions(fx_normal, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(fx_normal, c, 0).xyz * 2.0 - vec3(1.0);
}

fn fx_motion_vector(uv: vec2<f32>) -> vec2<f32> {
  let size = vec2<i32>(textureDimensions(fx_motion, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(fx_motion, c, 0).xy;
}

fn fx_view_depth(uv: vec2<f32>) -> f32 {
  let size = vec2<i32>(textureDimensions(fx_depth, 0));
  let c = clamp(vec2<i32>(uv * vec2<f32>(size)), vec2(0, 0), size - vec2(1, 1));
  return textureLoad(fx_depth, c, 0);
}
