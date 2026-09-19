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

struct Desc {
  position: vec4<f32>,
  extent: vec4<f32>,
  span: array<vec4<f32>, 3>,
  grid: vec4<u32>,
};

fn probe_dim(d: Desc) -> vec3<u32> {
  return max(d.grid.yzw, vec3<u32>(1u));
}

fn probe_position(d: Desc, index: u32) -> vec3<f32> {
  let dim = probe_dim(d);
  let c = vec3<f32>(vec3<u32>(index % dim.x, (index / dim.x) % dim.y,
    index / (dim.x * dim.y)));
  let last = vec3<f32>(dim) - vec3(1.0);
  let u = select(vec3(0.0), c / max(last, vec3(1.0)) - vec3(0.5), last > vec3(0.0));
  return d.position.xyz + d.span[0].xyz * u.x + d.span[1].xyz * u.y + d.span[2].xyz * u.z;
}
