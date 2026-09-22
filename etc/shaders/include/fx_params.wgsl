#pragma once

// Uniform block of a post process pass and the values every pass of a chain
// shares: the parameters of its component, the size of its target, the camera
// and the clip to world matrix of the view. eye.w is the display encoding of
// the frame, frame.z the encoding of the image the pass reads and frame.w the
// encoding of the image it writes.
struct Effect {
  params: array<vec4<f32>, EFFECT_PARAM_VECS>,
  size: vec4<f32>,
  eye: vec4<f32>,
  frame: vec4<f32>,
  clip: mat4x4<f32>,
  inv_clip: mat4x4<f32>,
};

// Adapted exposure of a view, written by the exposure compute passes.
struct Exposure {
  ev: f32,
  scale: f32,
  average: f32,
  pad: f32,
};

// A chain over a float scene target carries linear light and only its last pass
// encodes; a chain over an 8 bit one carries display encoded values and every
// pass decodes what it reads and encodes what it writes. Either way a pass that
// does neither is bit identical to the image it was handed.
fn fx_decode(c: vec3<f32>, encode: f32) -> vec3<f32> {
  let positive = max(c, vec3(0.0));
  if (encode > 0.5) {
    return pow(positive, vec3(2.2));
  }

  return positive;
}

fn fx_encode(c: vec3<f32>, encode: f32) -> vec3<f32> {
  let positive = max(c, vec3(0.0));
  if (encode > 0.5) {
    return pow(positive, vec3(1.0 / 2.2));
  }

  return positive;
}

fn fx_luma(c: vec3<f32>) -> f32 {
  return dot(max(c, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
}

// Ordered 8 by 8 Bayer threshold of a pixel, zero to one.
fn fx_bayer(p: vec2<u32>) -> f32 {
  let x = p.x & 7u;
  let y = p.y & 7u;
  let a = x ^ y;
  var v = ((y >> 2u) & 1u);
  v = v | (((a >> 2u) & 1u) << 1u);
  v = v | (((y >> 1u) & 1u) << 2u);
  v = v | (((a >> 1u) & 1u) << 3u);
  v = v | ((y & 1u) << 4u);
  v = v | ((a & 1u) << 5u);
  return f32(v) / 64.0;
}
