#pragma once

fn material_noise_hash(p : vec2<i32>, seed : u32) -> f32 {
  var h = bitcast<u32>(p.x) * 374761393u
        + bitcast<u32>(p.y) * 668265263u
        + seed * 2147483647u;
  h = (h ^ (h >> 13u)) * 1274126177u;
  h = h ^ (h >> 16u);
  return f32(h & 0xFFFFu) / 65535.0;
}

fn material_noise_gradient(c : vec2<i32>, seed : u32) -> vec2<f32> {
  let a = material_noise_hash(c, seed) * 6.28318530718;
  return vec2<f32>(cos(a), sin(a));
}

fn material_noise_value(p : vec2<f32>, seed : u32) -> f32 {
  let i = floor(p);
  let f = p - i;
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let c = vec2<i32>(i);
  let d00 = dot(material_noise_gradient(c, seed), f);
  let d10 = dot(material_noise_gradient(c + vec2<i32>(1, 0), seed),
    f - vec2<f32>(1.0, 0.0));
  let d01 = dot(material_noise_gradient(c + vec2<i32>(0, 1), seed),
    f - vec2<f32>(0.0, 1.0));
  let d11 = dot(material_noise_gradient(c + vec2<i32>(1, 1), seed),
    f - vec2<f32>(1.0, 1.0));
  let v = mix(mix(d00, d10, u.x), mix(d01, d11, u.x), u.y);
  return saturate(v * 0.70710678 + 0.5);
}

fn material_noise_fbm(p: vec2<f32>, footprint: f32, seed: u32) -> f32 {
  var sum = 0.0;
  var amp = 0.5;
  var total = 0.0;
  var q = p;
  var width = footprint;
  for (var i = 0u; i < 3u; i++) {
    let detail = 1.0 - smoothstep(0.35, 1.0, width);
    var value = 0.5;
    if (detail > 0.0) {
      value = mix(0.5, material_noise_value(q, seed + i), detail);
    }
    sum += amp * value;
    total += amp;
    amp *= 0.5;
    q = vec2(q.x * 1.6 - q.y * 1.2, q.x * 1.2 + q.y * 1.6);
    width *= 2.0;
  }
  return sum / total;
}

fn material_noise_triplanar(p: vec3<f32>, n: vec3<f32>, footprint: vec3<f32>,
    seed: u32) -> f32 {
  var w = abs(n);
  w = w * w * w;
  w /= max(w.x + w.y + w.z, 1e-4);
  var value = 0.0;
  if (w.x > 0.0) {
    value += material_noise_fbm(p.zy, length(footprint.zy), seed) * w.x;
  }
  if (w.y > 0.0) {
    value += material_noise_fbm(p.xz, length(footprint.xz), seed + 17u) * w.y;
  }
  if (w.z > 0.0) {
    value += material_noise_fbm(p.xy, length(footprint.xy), seed + 71u) * w.z;
  }
  return value;
}

fn material_noise_triplanar_value(p: vec3<f32>, n: vec3<f32>, seed: u32) -> f32 {
  var w = abs(n);
  w = w * w * w;
  w /= max(w.x + w.y + w.z, 1e-4);
  var value = 0.0;
  if (w.x > 0.0) {
    value += material_noise_value(p.zy, seed) * w.x;
  }
  if (w.y > 0.0) {
    value += material_noise_value(p.xz, seed + 17u) * w.y;
  }
  if (w.z > 0.0) {
    value += material_noise_value(p.xy, seed + 71u) * w.z;
  }
  return value;
}
