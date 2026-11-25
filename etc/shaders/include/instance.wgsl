// Instance data of a batch, one per slot of each stream of its instance page.
#pragma once
#include "surface.wgsl"
#include "render_origin.wgsl"

// The rows of the model matrix and the cell of the grid of ORIGIN_CELL a world record is placed
// in (box_render_transform_t): its translation is relative to the corner of the cell, so it
// stays precise anywhere in the world. cell.w is 1 for a world record, 0 for a record relative
// to something else, as a mover or a texture frame.
struct InstanceTransform {
    rows: array<vec4f, 3>,
    cell: vec4i,
};

// Edge of a cell of the grid the instances are placed on (BOX_RENDER_ORIGIN_CELL).
const ORIGIN_CELL: i32 = 1024;

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

// Offset of the translation of a record relative to the render origin of a pass (View.origin or
// Cascade.origin): the corner of its cell minus the origin for a world record, in whole units
// that convert exactly near the origin, else zero.
fn instance_offset(t: InstanceTransform, origin: vec4i) -> vec3f {
    var result = vec3f(0.0);
    if (t.cell.w != 0) {
        let o = render_origin(origin);
        result = vec3f(t.cell.xyz * ORIGIN_CELL - o.whole) - o.rest;
    }
    return result;
}

// The rows of a record relative to the render origin of a pass, its translation in w.
fn instance_rows(t: InstanceTransform, origin: vec4i) -> array<vec4f, 3> {
    let offset = instance_offset(t, origin);
    var r = t.rows;
    r[0].w += offset.x;
    r[1].w += offset.y;
    r[2].w += offset.z;
    return r;
}

// Model matrix of rows, its fourth row (0, 0, 0, 1).
fn rows_model(r: array<vec4f, 3>) -> mat4x4f {
    return mat4x4f(vec4f(r[0].x, r[1].x, r[2].x, 0.0), vec4f(r[0].y, r[1].y, r[2].y, 0.0),
        vec4f(r[0].z, r[1].z, r[2].z, 0.0), vec4f(r[0].w, r[1].w, r[2].w, 1.0));
}

// Model matrix of an instance relative to the render origin of a pass.
fn instance_model(t: InstanceTransform, origin: vec4i) -> mat4x4f {
    return rows_model(instance_rows(t, origin));
}

// Transform of an instance relative to its texture frame (MappingFrame), from its record of the
// frame stream; a zero record, which the pages without the frame stream bind, gives the model
// matrix, which projects as MappingWorld does. A world record (MappingObject) is relative to the
// render origin of the pass.
fn instance_frame(t: InstanceTransform, model: mat4x4f, origin: vec4i) -> mat4x4f {
    var result = model;
    if (any(t.rows[0].xyz != vec3f(0.0)) || any(t.rows[1].xyz != vec3f(0.0))) {
        result = instance_model(t, origin);
    }
    return result;
}

// Whether the record of the frame stream of an instance places it under a texture frame: it is
// not zero and not the record of its transform, which a shape without a frame has
// (box_draws_frame_record), so that it projects from its world position.
fn instance_framed(frame: InstanceTransform, transform: InstanceTransform) -> bool {
    let placed = any(frame.rows[0].xyz != vec3f(0.0)) || any(frame.rows[1].xyz != vec3f(0.0));
    let own = all(frame.rows[0] == transform.rows[0]) &&
        all(frame.rows[1] == transform.rows[1]) && all(frame.rows[2] == transform.rows[2]) &&
        all(frame.cell == transform.cell);
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
