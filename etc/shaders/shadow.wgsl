// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The cull pass writes the slots of the casters inside the cascade, which step per
// instance; only the model matrix of the instance is read.
#include "include/instance.wgsl"

struct Cascade {
    matrix: mat4x4f,
};

@group(0) @binding(0) var<uniform> cascade: Cascade;
@group(1) @binding(0) var<storage, read> instances: array<Instance>;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) slot: u32,
};

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    let model = instance_model(instances[v.slot]);
    return cascade.matrix * (model * vec4f(v.position, 1.0));
}
