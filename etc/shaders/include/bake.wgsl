#pragma once

struct Emitter {
  position: vec3<f32>,
  radius: f32,
  color: vec3<f32>,
  range: f32,
  mesh: u32,
  slot: u32,
  pad0: u32,
  pad1: u32,
};
