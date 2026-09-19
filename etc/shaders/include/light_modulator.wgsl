#pragma once

// Parameter block of one light modulator: the floats of the component the
// plugin registered with box_light_modulator_params, in member order and four
// to a vector, so a CloudShadows {scale, speed, density, softness} arrives as
// params.a. A member the component does not have reads zero, which is what
// lets a plugin give it a default of its own.
struct LightModulatorParams {
  a: vec4<f32>,
  b: vec4<f32>,
  c: vec4<f32>,
  d: vec4<f32>,
};

fn box_modulator_hash(p: vec2<f32>) -> f32 {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

fn box_modulator_noise(p: vec2<f32>) -> f32 {
  let cell = floor(p);
  let f = fract(p);
  let u = f * f * (vec2(3.0) - 2.0 * f);
  let a = box_modulator_hash(cell);
  let b = box_modulator_hash(cell + vec2(1.0, 0.0));
  let c = box_modulator_hash(cell + vec2(0.0, 1.0));
  let d = box_modulator_hash(cell + vec2(1.0, 1.0));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Three octaves of value noise, which is enough shape for a cloud deck and
// cheap enough to evaluate once per shaded point.
fn box_modulator_fbm(p: vec2<f32>) -> f32 {
  var sum = 0.0;
  var amplitude = 0.5;
  var q = p;
  for (var i = 0u; i < 3u; i = i + 1u) {
    sum = sum + box_modulator_noise(q) * amplitude;
    q = q * 2.03 + vec2(17.3, 5.1);
    amplitude = amplitude * 0.5;
  }
  return sum / 0.875;
}
