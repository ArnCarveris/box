#pragma once

fn radical_inverse(index: u32) -> f32 {
  var bits = index;
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, count: u32) -> vec2<f32> {
  return vec2(f32(i) / f32(count), radical_inverse(i));
}

fn tangent_to_world(h: vec3<f32>, n: vec3<f32>) -> vec3<f32> {
  var up = vec3(0.0, 0.0, 1.0);
  if (abs(n.z) > 0.999) { up = vec3(1.0, 0.0, 0.0); }
  let tangent = normalize(cross(up, n));
  let bitangent = cross(n, tangent);
  return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

fn importance_ggx(xi: vec2<f32>, n: vec3<f32>, roughness: f32) -> vec3<f32> {
  let a = roughness * roughness;
  let phi = 6.28318531 * xi.x;
  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
  let h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
  return tangent_to_world(h, n);
}
