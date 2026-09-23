#include "include/view.wgsl"
#include "include/light.wgsl"

@group(0) @binding(0) var<uniform> u: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read_write> clusters: array<vec4<u32>>;
@group(0) @binding(3) var<storage, read_write> light_indices: array<u32>;

fn slice_depth(slice: f32) -> f32 {
  return u.cluster_near * pow(u.cluster_far / u.cluster_near, slice);
}

@compute @workgroup_size(4, 4, 4)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= u.cluster_x || gid.y >= u.cluster_y || gid.z >= u.cluster_z) {
    return;
  }
  let cluster = gid.x + gid.y * u.cluster_x + gid.z * u.cluster_x * u.cluster_y;
  let z0 = slice_depth(f32(gid.z) / f32(u.cluster_z));
  let z1 = slice_depth(f32(gid.z + 1u) / f32(u.cluster_z));
  let tx = 1.0 / u.proj[0][0];
  let ty = 1.0 / u.proj[1][1];
  let x0 = (f32(gid.x) / f32(u.cluster_x)) * 2.0 - 1.0;
  let x1 = (f32(gid.x + 1u) / f32(u.cluster_x)) * 2.0 - 1.0;
  let y1 = 1.0 - (f32(gid.y) / f32(u.cluster_y)) * 2.0;
  let y0 = 1.0 - (f32(gid.y + 1u) / f32(u.cluster_y)) * 2.0;
  let cx = vec4(x0 * tx * z0, x0 * tx * z1, x1 * tx * z0, x1 * tx * z1);
  let cy = vec4(y0 * ty * z0, y0 * ty * z1, y1 * ty * z0, y1 * ty * z1);
  let lo = vec3(min(min(cx.x, cx.y), min(cx.z, cx.w)),
                min(min(cy.x, cy.y), min(cy.z, cy.w)), z0);
  let hi = vec3(max(max(cx.x, cx.y), max(cx.z, cx.w)),
                max(max(cy.x, cy.y), max(cy.z, cy.w)), z1);
  let base = cluster * u.cluster_max * 2u;
  let dynamic_base = base + u.cluster_max;
  var dynamic_count = 0u;
  var count = 0u;
  for (var i = 0u; i < u.light_count && count < u.cluster_max; i = i + 1u) {
    let p = (u.view * vec4(lights[i].position, 1.0)).xyz;
    let d = p - clamp(p, lo, hi);
    if (dot(d, d) <= lights[i].range * lights[i].range) {
      light_indices[base + count] = i;
      if (lights[i].is_static == 0u) {
        light_indices[dynamic_base + dynamic_count] = i;
        dynamic_count += 1u;
      }
      count = count + 1u;
    }
  }
  clusters[cluster] = vec4(base, count, dynamic_base, dynamic_count);
}
