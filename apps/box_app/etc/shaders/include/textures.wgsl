#pragma once

@group(2) @binding(4) var tex0: texture_2d_array<f32>;
@group(2) @binding(5) var tex1: texture_2d_array<f32>;
@group(2) @binding(6) var tex2: texture_2d_array<f32>;
@group(2) @binding(7) var tex3: texture_2d_array<f32>;
@group(2) @binding(8) var samp0: sampler;
@group(2) @binding(9) var samp1: sampler;
@group(2) @binding(10) var samp2: sampler;
@group(2) @binding(11) var samp3: sampler;
@group(2) @binding(12) var scene_tex: texture_2d<f32>;
@group(2) @binding(13) var scene_samp: sampler;

fn tex_sample(slot: u32, uv: vec2<f32>, ddx: vec2<f32>, ddy: vec2<f32>) -> vec4<f32> {
  let cls = slot >> 14u;
  let layer = (slot & 0x3FFFu) - 1u;
  var result = vec4<f32>(1.0);
  if (cls == 0u) {
    result = textureSampleGrad(tex0, samp0, uv, layer, ddx, ddy);
  } else if (cls == 1u) {
    result = textureSampleGrad(tex1, samp1, uv, layer, ddx, ddy);
  } else if (cls == 2u) {
    result = textureSampleGrad(tex2, samp2, uv, layer, ddx, ddy);
  } else {
    result = textureSampleGrad(tex3, samp3, uv, layer, ddx, ddy);
  }
  return result;
}

fn perturb(n: vec3<f32>, dp1: vec3<f32>, dp2: vec3<f32>, duv1: vec2<f32>,
    duv2: vec2<f32>, ts: vec3<f32>) -> vec3<f32> {
  let perp1 = cross(dp2, n);
  let perp2 = cross(n, dp1);
  let t = perp1 * duv1.x + perp2 * duv2.x;
  let b = perp1 * duv1.y + perp2 * duv2.y;
  let area = dot(n, cross(dp1, dp2));
  let side = select(-1.0, 1.0, area >= 0.0);
  let inv = side * inverseSqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
  return normalize(t * (inv * ts.x) + b * (inv * ts.y) + n * ts.z);
}
