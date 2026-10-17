// Instance data of a batch, one per slot of its instance buffer (box_render_instance_t). The
// model matrix is affine with the instance scale; the w of its first three columns is unused,
// except that a 2D instance keeps its clip index in the w of the first (shape2d.wgsl).
#pragma once

struct Instance {
    model: mat4x4f,
    color: vec4f,
    emissive: vec4f,
    material: vec4f,
    uv: vec4f,
};

// Model matrix with the unused w of the first three columns cleared.
fn instance_model(i: Instance) -> mat4x4f {
    return mat4x4f(vec4f(i.model[0].xyz, 0.0), vec4f(i.model[1].xyz, 0.0),
        vec4f(i.model[2].xyz, 0.0), vec4f(i.model[3].xyz, 1.0));
}
