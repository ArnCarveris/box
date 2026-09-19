#pragma once

// The pages of the texture pager, one array texture each. A packed slot holds
// the size class in its top two bits and the layer plus one below them, and the
// layer counts across the pages of its class: the high bits name the page and
// the low eight bits the layer inside it, because a page holds at most the 256
// array layers WebGPU guarantees. The page count matches BOX_TEXTURE_PAGES and
// the pages of every class share this list, so a draw reaches any texture of
// the scene without changing bind groups.
@group(2) @binding(4) var tex_page0: texture_2d_array<f32>;
@group(2) @binding(5) var tex_page1: texture_2d_array<f32>;
@group(2) @binding(6) var tex_page2: texture_2d_array<f32>;
@group(2) @binding(7) var tex_page3: texture_2d_array<f32>;
@group(2) @binding(8) var tex_page4: texture_2d_array<f32>;
@group(2) @binding(9) var tex_page5: texture_2d_array<f32>;
@group(2) @binding(10) var tex_page6: texture_2d_array<f32>;
@group(2) @binding(11) var tex_page7: texture_2d_array<f32>;
@group(2) @binding(12) var tex_samp: sampler;
@group(2) @binding(13) var scene_tex: texture_2d<f32>;
@group(2) @binding(14) var scene_samp: sampler;

fn tex_sample(slot: u32, uv: vec2<f32>, ddx: vec2<f32>, ddy: vec2<f32>) -> vec4<f32> {
  let index = (slot & 0x3FFFu) - 1u;
  let page = index >> 8u;
  let layer = index & 0xFFu;
  var result = vec4<f32>(1.0);
  switch (page) {
    case 0u: {
      result = textureSampleGrad(tex_page0, tex_samp, uv, layer, ddx, ddy);
    }
    case 1u: {
      result = textureSampleGrad(tex_page1, tex_samp, uv, layer, ddx, ddy);
    }
    case 2u: {
      result = textureSampleGrad(tex_page2, tex_samp, uv, layer, ddx, ddy);
    }
    case 3u: {
      result = textureSampleGrad(tex_page3, tex_samp, uv, layer, ddx, ddy);
    }
    case 4u: {
      result = textureSampleGrad(tex_page4, tex_samp, uv, layer, ddx, ddy);
    }
    case 5u: {
      result = textureSampleGrad(tex_page5, tex_samp, uv, layer, ddx, ddy);
    }
    case 6u: {
      result = textureSampleGrad(tex_page6, tex_samp, uv, layer, ddx, ddy);
    }
    default: {
      result = textureSampleGrad(tex_page7, tex_samp, uv, layer, ddx, ddy);
    }
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
