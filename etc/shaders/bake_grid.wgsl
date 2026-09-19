#include "include/bake.wgsl"
#include "include/probe.wgsl"

@group(0) @binding(0) var<uniform> u: Bake;
@group(0) @binding(1) var<storage, read_write> occupancy: array<atomic<u32>>;
@group(1) @binding(0) var<storage, read> descs: array<Desc>;
@group(1) @binding(1) var<uniform> info: Info;

@compute @workgroup_size(BAKE_GROUP)
fn cs_grid(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= info.desc_count || u.grid_dim.x == 0u) {
    return;
  }
  let d = descs[gid.x];
  if (d.extent.w == 0.0) {
    return;
  }
  let limit = vec3<f32>(u.grid_dim.xyz) - vec3(1.0);
  let lo = (d.position.xyz - d.extent.xyz - u.grid_min.xyz) / u.grid_cell.xyz;
  let hi = (d.position.xyz + d.extent.xyz - u.grid_min.xyz) / u.grid_cell.xyz;
  let a = vec3<u32>(clamp(lo, vec3(0.0), limit));
  let b = vec3<u32>(clamp(hi, vec3(0.0), limit));
  for (var z = a.z; z <= b.z; z = z + 1u) {
    for (var y = a.y; y <= b.y; y = y + 1u) {
      for (var x = a.x; x <= b.x; x = x + 1u) {
        let index = x + y * u.grid_dim.x + z * u.grid_dim.x * u.grid_dim.y;
        atomicOr(&occupancy[index >> 5u], 1u << (index & 31u));
      }
    }
  }
}
