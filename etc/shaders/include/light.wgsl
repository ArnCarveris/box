#pragma once

struct Light {
  position: vec3<f32>,
  range: f32,
  color: vec3<f32>,
  intensity: f32,
  direction: vec3<f32>,
  cos_inner: f32,
  cos_outer: f32,
  kind: u32,
  is_static: u32,
  modulator: u32,
};
