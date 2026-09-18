#pragma once

#include "fullscreen.wgsl"
#include "fx_params.wgsl"

@group(0) @binding(0) var<uniform> fx: Effect;
@group(0) @binding(1) var fx_color: texture_2d<f32>;
@group(0) @binding(2) var fx_sampler: sampler;
@group(0) @binding(3) var fx_depth: texture_depth_2d;
@group(0) @binding(4) var fx_normal: texture_2d<f32>;
@group(0) @binding(5) var fx_motion: texture_2d<f32>;
@group(0) @binding(6) var<storage, read> fx_exposure: Exposure;
@group(0) @binding(7) var fx_pyramid: texture_2d<f32>;

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

// Blurred pyramid of the image the pass reads, in linear light.
fn fx_pyramid_sample(uv: vec2<f32>) -> vec3<f32> {
  return textureSampleLevel(fx_pyramid, fx_sampler, uv, 0.0).rgb;
}

// Multiplier the adapted exposure of the view asks for, one until a pass with
// the exposure input has run.
fn fx_exposure_scale() -> f32 {
  return fx_exposure.scale;
}

fn fx_to_linear(c: vec3<f32>) -> vec3<f32> {
  return fx_decode(c, fx.eye.w);
}

fn fx_to_display(c: vec3<f32>) -> vec3<f32> {
  return fx_encode(c, fx.eye.w);
}

// World position the depth of a pixel stands for, reconstructed with the clip
// to world matrix of the view.
fn fx_world_pos(uv: vec2<f32>, depth: f32) -> vec3<f32> {
  let ndc = vec3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, max(depth, 0.0000001));
  let p = fx.inv_clip * vec4(ndc, 1.0);
  return p.xyz / p.w;
}
