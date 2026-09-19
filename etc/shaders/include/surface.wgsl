#pragma once

// Per entity amounts of the material layers of one instance.
//
// Every amount is a factor of what the scene set on its layer, so an instance
// without a Wear component reads ones and a layer runs as it always did. The
// seed offsets the noise of every layer so two copies of a prefab differ, and
// the scale multiplies the feature frequency of every layer.
struct Wear {
  dust: f32,
  grime: f32,
  edge: f32,
  wet: f32,
  burn: f32,
  scratch: f32,
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
  wear: Wear,
};

// Mode word bit that says the wear words hold amounts, BOX_MODE_WEAR_SHIFT.
const WEAR_BIT: u32 = 0x20u;

// Turns the unit range of the scale byte back into steps of one over 64, which
// is BOX_WEAR_SCALE_STEP, so a feature scale of one comes back as one.
const WEAR_SCALE_DECODE: f32 = 3.984375;

// World units the noise of an instance is offset by per unit of its seed.
const WEAR_SEED_SPAN: f32 = 97.0;

// Unpacks the two wear words of an instance, or the neutral wear without them.
fn wear_unpack(on: bool, a: u32, b: u32) -> Wear {
  var w: Wear;
  w.dust = 1.0;
  w.grime = 1.0;
  w.edge = 1.0;
  w.wet = 1.0;
  w.burn = 1.0;
  w.scratch = 1.0;
  w.seed = 0.0;
  w.scale = 1.0;
  if (on) {
    let first = unpack4x8unorm(a);
    let second = unpack4x8unorm(b);
    w.dust = first.x;
    w.grime = first.y;
    w.edge = first.z;
    w.wet = first.w;
    w.burn = second.x;
    w.scratch = second.y;
    w.seed = second.z;
    w.scale = second.w * WEAR_SCALE_DECODE;
  }
  return w;
}

// Value noise of a layer at the world position of a surface, seeded and scaled
// by the wear of its instance, so a layer reads one frequency and gets both.
fn wear_noise(s: ptr<function, Surface>, frequency: f32) -> f32 {
  let w = (*s).wear;
  let origin = (*s).world_pos + vec3<f32>(w.seed * WEAR_SEED_SPAN);
  return box_noise(origin * (frequency * w.scale));
}

struct LayerParams {
  a: vec4<f32>,
  b: vec4<f32>,
};

struct LayerBlock {
  p: array<LayerParams, LAYER_MAX>,
};

@group(0) @binding(1) var<uniform> layer_params: LayerBlock;
