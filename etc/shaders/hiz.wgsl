#include "include/hiz.wgsl"

@compute @workgroup_size(HIZ_GROUP)
fn cs_first(@builtin(global_invocation_id) id: vec3<u32>) {
#include "include/hiz_bounds.wgsl"
  if (shown_before(base, center, radius)) {
    append(base);
  }
}

@compute @workgroup_size(HIZ_GROUP)
fn cs_second(@builtin(global_invocation_id) id: vec3<u32>) {
#include "include/hiz_bounds.wgsl"
  atomicAdd(&counters[0], 1u);
  if (!shown_now(base, center, radius)) {
    atomicAdd(&counters[1], 1u);
    return;
  }
  if (!shown_before(base, center, radius)) {
    append(base);
  }
}
