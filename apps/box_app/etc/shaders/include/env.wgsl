#pragma once

struct Env {
  sky: vec4<f32>,
  ground: vec4<f32>,
  params: vec4<f32>,
};

@group(0) @binding(0) var<uniform> e: Env;

struct EnvOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

@vertex fn vs_main(@builtin(vertex_index) i: u32) -> EnvOut {
  var p = array<vec2<f32>, 3>(vec2(-1.0, -3.0), vec2(-1.0, 1.0), vec2(3.0, 1.0));
  var out: EnvOut;
  out.clip = vec4(p[i], 0.0, 1.0);
  out.uv = vec2(p[i].x * 0.5 + 0.5, 0.5 - p[i].y * 0.5);
  return out;
}

fn face_dir(face: u32, uv: vec2<f32>) -> vec3<f32> {
  let s = uv.x * 2.0 - 1.0;
  let t = uv.y * 2.0 - 1.0;
  var d = vec3(s, -t, 1.0);
  if (face == 0u) { d = vec3(1.0, -t, -s); }
  else if (face == 1u) { d = vec3(-1.0, -t, s); }
  else if (face == 2u) { d = vec3(s, 1.0, t); }
  else if (face == 3u) { d = vec3(s, -1.0, -t); }
  else if (face == 5u) { d = vec3(-s, -t, -1.0); }
  return normalize(d);
}
