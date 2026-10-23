// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The cull pass writes the slots of the casters inside the cascade, which step per
// instance; only the model matrix of the instance is read.
#include "include/instance.wgsl"

struct Cascade {
    matrix: mat4x4f,
};

@group(0) @binding(0) var<uniform> cascade: Cascade;
@group(1) @binding(0) var<storage, read> instances: array<Instance>;
@group(1) @binding(1) var<storage, read> surface_assets: array<vec4f>;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) slot: u32,
};

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    let inst = instances[v.slot];
    surface_data = inst.surface;
    let model = instance_model(inst);
    var position = v.position;
    if (inst.surface.p[19].x != 0.0) {
        position = v.position;
    } else if (inst.surface.p[0].x == 7.0) {
        position.y *= inst.surface.p[1].y;
    } else if (inst.surface.p[0].x != 0.0) {
        position = surface_position(i32(position.z), position.x, position.y);
    }
    return cascade.matrix * (model * vec4f(position, 1.0));
}
