#pragma once

struct Probe {
  r: vec4<f32>,
  g: vec4<f32>,
  b: vec4<f32>,
};

const SH_Y0: f32 = 0.282095;
const SH_Y1: f32 = 0.488603;
const SH_A0: f32 = 0.886227;
const SH_A1: f32 = 1.023327;
