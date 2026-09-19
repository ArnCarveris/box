#pragma once

fn cluster_index(vp: vec3<f32>) -> u32 {
  let depth = max(vp.z, 1e-4);
  let ndc_x = u.proj[0][0] * vp.x / depth;
  let ndc_y = u.proj[1][1] * vp.y / depth;
  let fx = (ndc_x + 1.0) * 0.5 * f32(u.cluster_x);
  let fy = (1.0 - ndc_y) * 0.5 * f32(u.cluster_y);
  let t = log(depth / u.cluster_near) / log(u.cluster_far / u.cluster_near);
  let fz = t * f32(u.cluster_z);
  let x = min(u32(max(fx, 0.0)), u.cluster_x - 1u);
  let y = min(u32(max(fy, 0.0)), u.cluster_y - 1u);
  let z = min(u32(max(fz, 0.0)), u.cluster_z - 1u);
  return x + y * u.cluster_x + z * u.cluster_x * u.cluster_y;
}
