#pragma once

#include "material_noise.wgsl"

// Per entity amounts of the material layers of one instance.
//
// Every amount is a factor of what the scene set on its layer, so an instance
// without a Wear component reads ones and a layer runs as it always did. The
// seed offsets the noise of every layer so two copies of a prefab differ, and
// the scale multiplies the feature frequency of every layer.
struct MaterialLayerData {
  amounts0: vec4<f32>,
  amounts1: vec2<f32>,
  seed: f32,
  scale: f32,
};

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
  local_footprint: vec3<f32>,
  world_footprint: vec3<f32>,
  layer_pos: vec3<f32>,
  layer_normal: vec3<f32>,
  layer_footprint: vec3<f32>,
  layer_extent: vec3<f32>,
  footprint: f32,
  curvature: f32,
  edge_bounds: bool,
  layer_data: MaterialLayerData,
};

// Mode word bit that says the wear words hold amounts, BOX_MODE_WEAR_SHIFT.
const LAYER_DATA_BIT: u32 = 0x20u;

// Turns the unit range of the scale byte back into steps of one over 64, which
// is BOX_WEAR_SCALE_STEP, so a feature scale of one comes back as one.
const LAYER_DATA_SCALE_DECODE: f32 = 3.984375;

// Unpacks the two wear words of an instance, or the neutral wear without them.
fn material_layer_data_unpack(on: bool, a: u32, b: u32) -> MaterialLayerData {
  var data: MaterialLayerData;
  data.amounts0 = vec4(1.0);
  data.amounts1 = vec2(1.0);
  data.seed = 0.0;
  data.scale = 1.0;
  if (on) {
    let first = unpack4x8unorm(a);
    let second = unpack4x8unorm(b);
    data.amounts0 = first;
    data.amounts1 = second.xy;
    data.seed = second.z;
    data.scale = second.w * LAYER_DATA_SCALE_DECODE;
  }
  return data;
}

fn material_layer_amount(s: ptr<function, Surface>, index: u32) -> f32 {
  if (index < 4u) {
    return (*s).layer_data.amounts0[index];
  }
  if (index < 6u) {
    return (*s).layer_data.amounts1[index - 4u];
  }
  return 1.0;
}

fn material_layer_seed(s: ptr<function, Surface>) -> u32 {
  return u32(round((*s).layer_data.seed * 255.0));
}

fn material_layer_origin(s: ptr<function, Surface>) -> vec3<f32> {
  let seed = material_layer_seed(s);
  let cell = vec2<i32>(i32(seed), 0);
  let offset = vec3(material_noise_hash(cell, 3u), material_noise_hash(cell, 7u),
    material_noise_hash(cell, 11u)) * 64.0;
  return (*s).layer_pos + offset;
}

fn material_layer_noise_scaled(s: ptr<function, Surface>, frequency: vec3<f32>) -> f32 {
  let origin = material_layer_origin(s);
  let scale = frequency * (*s).layer_data.scale;
  return material_noise_triplanar(origin * scale, (*s).layer_normal,
    (*s).layer_footprint * abs(scale), material_layer_seed(s));
}

fn material_layer_noise(s: ptr<function, Surface>, frequency: f32) -> f32 {
  return material_layer_noise_scaled(s, vec3(frequency));
}

struct LayerParams {
  a: vec4<f32>,
  b: vec4<f32>,
};

struct LayerBlock {
  p: array<LayerParams, LAYER_MAX>,
};

@group(0) @binding(1) var<uniform> layer_params: LayerBlock;
