#pragma once

// The block of one emitter, shared by the simulation and the sprite pass so the
// two never drift apart. Its layout is box_particle_emitter_t up to its id, and
// src/particle/simulate.h is the C twin of the functions below, which the tests
// check the draws, the integrator and the curves against.

struct Emitter {
  row0: vec4<f32>,
  row1: vec4<f32>,
  row2: vec4<f32>,
  color: vec4<f32>,
  color_max: vec4<f32>,
  acceleration: vec4<f32>,
  wind: vec4<f32>,
  collision: vec4<f32>,
  direction: vec4<f32>,
  extents: vec4<f32>,
  size_curve: vec4<f32>,
  opacity_curve: vec4<f32>,
  turn_curve: vec4<f32>,
  gradient: array<vec4<f32>, 4>,
  sheet: vec4<f32>,
  speed: vec2<f32>,
  lifetime: vec2<f32>,
  size: vec2<f32>,
  rotation: vec2<f32>,
  opacity: vec2<f32>,
  delta: f32,
  pad: f32,
  count: u32,
  spawn: u32,
  first: u32,
  shape: u32,
  seed: u32,
  points: u32,
  flags: u32,
  colliders: u32,
  appearance: u32,
  texture: u32,
  pad2: u32,
  pad3: u32,
};

// One entry of the table of an emitter: a spawn point of its shape or, behind
// those, a collider its particles bounce off.
struct Point {
  position: vec4<f32>,
  normal: vec4<f32>,
};

const RESET: u32 = 1u;
const GROUND: u32 = 2u;
const DIE: u32 = 4u;
const ADDITIVE: u32 = 8u;
const SOFT: u32 = 16u;
const PAUSED: u32 = 32u;

const DRAW_CIRCLE: u32 = 0u;
const DRAW_SPRITE: u32 = 1u;
const DRAW_BILLBOARD: u32 = 2u;

// Value of a four key curve at a normalised age, the line between the two keys
// the age falls between.
fn curve_at(keys: vec4<f32>, t: f32) -> f32 {
  let x = clamp(t, 0.0, 1.0) * 3.0;
  let i = min(u32(x), 2u);
  let f = x - f32(i);
  return mix(keys[i], keys[i + 1u], f);
}

// How much of a particle is left where it approaches the geometry behind it.
fn soft_at(scene_z: f32, particle_z: f32, softness: f32) -> f32 {
  if (softness <= 0.0) {
    return 1.0;
  }

  return clamp((scene_z - particle_z) / softness, 0.0, 1.0);
}
