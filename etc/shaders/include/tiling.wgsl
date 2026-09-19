#pragma once

fn tile_uv(mode: u32, xform: vec4<f32>, mesh_uv: vec2<f32>, p: vec3<f32>,
    n: vec3<f32>) -> vec2<f32> {
  let proj = mode & 0xFu;
  let a = abs(n);
  let r = length(p.xz);
  let angle = atan2(p.z, p.x);
  var base = mesh_uv;
  if (proj == 1u) {
    if (a.x >= a.y && a.x >= a.z) {
      base = vec2(select(p.z, -p.z, n.x < 0.0), p.y);
    } else if (a.y >= a.z) {
      base = vec2(p.x, select(p.z, -p.z, n.y < 0.0));
    } else {
      base = vec2(select(-p.x, p.x, n.z < 0.0), p.y);
    }
  } else if (proj == 2u) {
    base = vec2(angle * r, p.y);
  } else if (proj == 3u) {
    base = vec2(r, angle * r);
  }
  return base * xform.xy + xform.zw;
}

fn wrap_uv(mode: u32, uv: vec2<f32>) -> vec2<f32> {
  let w = (mode >> 4u) & 0xFu;
  var result = uv;
  if (w == 1u) {
    result = clamp(uv, vec2(0.0), vec2(1.0));
  } else if (w == 2u) {
    let t = uv - 2.0 * floor(uv * 0.5);
    result = vec2(1.0) - abs(t - vec2(1.0));
  }
  return result;
}
