#pragma once

fn srgb_to_linear(c: vec3<f32>) -> vec3<f32> {
  let lo = c / 12.92;
  let hi = pow((c + vec3(0.055)) / 1.055, vec3(2.4));
  return select(lo, hi, c > vec3(0.04045));
}

fn box_hash(p: vec3<f32>) -> f32 {
  let q = fract(p * vec3(0.1031, 0.1030, 0.0973));
  let r = q + vec3(dot(q, q.yzx + vec3(33.33)));
  return fract((r.x + r.y) * r.z);
}

fn box_noise(p: vec3<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (vec3(3.0) - 2.0 * f);
  let x00 = mix(box_hash(i), box_hash(i + vec3(1.0, 0.0, 0.0)), u.x);
  let x10 = mix(box_hash(i + vec3(0.0, 1.0, 0.0)),
    box_hash(i + vec3(1.0, 1.0, 0.0)), u.x);
  let x01 = mix(box_hash(i + vec3(0.0, 0.0, 1.0)),
    box_hash(i + vec3(1.0, 0.0, 1.0)), u.x);
  let x11 = mix(box_hash(i + vec3(0.0, 1.0, 1.0)),
    box_hash(i + vec3(1.0, 1.0, 1.0)), u.x);
  return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}
