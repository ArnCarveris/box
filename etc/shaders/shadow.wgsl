#include "include/spline.wgsl"

struct Cascade {
  matrix: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> c: Cascade;

struct VertexIn {
  @location(0) pos: vec3<f32>,
  @location(3) row0: vec4<f32>,
  @location(4) row1: vec4<f32>,
  @location(5) row2: vec4<f32>,
  @location(17) sweep_center: vec3<f32>,
  @location(18) sweep_tangent: vec3<f32>,
  @location(19) sweep_profile: vec4<f32>,
};

@vertex fn vs_main(in: VertexIn) -> @builtin(position) vec4<f32> {
  let m = mat4x4<f32>(
    vec4(in.row0.x, in.row1.x, in.row2.x, 0.0),
    vec4(in.row0.y, in.row1.y, in.row2.y, 0.0),
    vec4(in.row0.z, in.row1.z, in.row2.z, 0.0),
    vec4(in.row0.w, in.row1.w, in.row2.w, 1.0));
  let sweep = spline_sweep(in.pos, vec3(0.0, 1.0, 0.0), in.sweep_center,
    in.sweep_tangent, in.sweep_profile);
  return c.matrix * (m * vec4(sweep.position, 1.0));
}
