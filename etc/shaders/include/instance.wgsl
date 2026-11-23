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

// Transform of an instance relative to its texture frame (MappingFrame), from its record of the
// frame stream; a zero record, which the pages without the frame stream bind, gives the model
// matrix, which projects as MappingWorld does.
fn instance_frame(t: InstanceTransform, model: mat4x4f) -> mat4x4f {
    var result = model;
    if (any(t.rows[0].xyz != vec3f(0.0)) || any(t.rows[1].xyz != vec3f(0.0))) {
        result = instance_model(t);
    }
    return result;
}

// Whether the record of the frame stream of an instance places it under a texture frame: it is
// not zero and not the record of its transform, which a shape without a frame has
// (box_draws_frame_record), so that it projects from its world position.
fn instance_framed(frame: InstanceTransform, transform: InstanceTransform) -> bool {
    let placed = any(frame.rows[0].xyz != vec3f(0.0)) || any(frame.rows[1].xyz != vec3f(0.0));
    let own = all(frame.rows[0] == transform.rows[0]) &&
        all(frame.rows[1] == transform.rows[1]) && all(frame.rows[2] == transform.rows[2]);
    return placed && !own;
}

// Per vertex data of a mesh that has it (Mesh3.data0 to data3, box_mesh_vertex_data and
// box_mesh_vertex_data_upper), a vertex stream of its own that only the _data entry points read:
// the pipelines of the batches whose mesh has data (box_gpu_vertex_data) bind it as the third
// vertex buffer.
struct VertexData {
    @location(4) data0: vec4f,
    @location(5) data1: vec4f,
    @location(6) data2: vec4f,
    @location(7) data3: vec4f,
};
