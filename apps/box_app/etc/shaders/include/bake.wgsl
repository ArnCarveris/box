#pragma once

struct Bake {
  grid_min: vec4<f32>,
  grid_cell: vec4<f32>,
  grid_dim: vec4<u32>,
  light_count: u32,
  emitter_count: u32,
  steps: u32,
  pad: u32,
};

struct Emitter {
  position: vec3<f32>,
  radius: f32,
  color: vec3<f32>,
  range: f32,
};

struct Job {
  slot: u32,
  index: u32,
};

struct Info {
  desc_count: u32,
  job_count: u32,
  pad0: u32,
  pad1: u32,
};
