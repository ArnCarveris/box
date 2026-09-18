#pragma once

struct Surface {
  albedo: vec3<f32>,
  alpha: f32,
  emissive: vec3<f32>,
  roughness: f32,
  metallic: f32,
  normal: vec3<f32>,
  geo_normal: vec3<f32>,
  world_pos: vec3<f32>,
  local: vec3<f32>,
};

struct LayerParams {
  a: vec4<f32>,
  b: vec4<f32>,
};

struct LayerBlock {
  p: array<LayerParams, LAYER_MAX>,
};

@group(0) @binding(1) var<uniform> layer_params: LayerBlock;
