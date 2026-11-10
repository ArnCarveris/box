// Instance data of a batch, one per slot of each stream of its instance page.
#pragma once
#include "surface.wgsl"

struct InstanceTransform {
    rows: array<vec4f, 3>,
};

struct MeshShading {
    material: u32,
    // Add red | add green << 16, add blue | keep << 16, unorm16
    tint_rg: u32,
    tint_bk: u32,
    alpha: f32,
};

// Composed tint of a mesh instance: xyz is added to the display encoded color times w.
fn mesh_shading_tint(s: MeshShading) -> vec4f {
    return vec4f(unpack2x16unorm(s.tint_rg), unpack2x16unorm(s.tint_bk));
}

// Model matrix of an instance, its fourth row (0, 0, 0, 1).
fn instance_model(t: InstanceTransform) -> mat4x4f {
    let r = t.rows;
    return mat4x4f(vec4f(r[0].x, r[1].x, r[2].x, 0.0), vec4f(r[0].y, r[1].y, r[2].y, 0.0),
        vec4f(r[0].z, r[1].z, r[2].z, 0.0), vec4f(r[0].w, r[1].w, r[2].w, 1.0));
}
