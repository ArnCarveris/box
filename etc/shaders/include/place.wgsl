// Placement of the vertices of a mesh instance, shared by the vertex stages of mesh.wgsl and
// debug_wire.wgsl. The includer declares view (group 0) and the transforms, surface_assets and
// surfaces streams of the instance group (group 2, bindings 0, 1 and 3).
#pragma once
#include "instance.wgsl"

struct VertexIn {
    @builtin(vertex_index) vertex: u32,
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) slot: u32,
};

// A vertex placed in the world; normal is in the frame of the instance. vs and the depth prepass
// both place vertices with it so the forward pass can depth test the prepass with Equal. It reads
// only the bindings the plain prepass has, and sets surface_data. A plain mesh page binds one zero
// record as its surface stream, so a slot past it leaves surface_data zero (the plain mesh path).
struct Placed {
    model: mat4x4f,
    normal: vec3f,
    world: vec4f,
    pos: vec4f,
};

fn place_at(v: VertexIn, position: vec3f, normal: vec3f) -> Placed {
    let model = instance_model(transforms[v.slot]);
    let world = model * vec4f(position, 1.0);
    return Placed(model, normal, world, view.view_proj * world);
}

fn place_mesh(v: VertexIn) -> Placed {
    return place_at(v, v.position, v.normal);
}

fn place(v: VertexIn) -> Placed {
    if (v.slot < arrayLength(&surfaces)) {
        surface_data = surfaces[v.slot];
    }
    var position = v.position;
    var normal = v.normal;
    // One call of surface_normal for both branches that need it, as D3D compilers inline each
    // call (see surface_normal).
    var derive = false;
    if (surface_data.p[19].x != 0.0) {
        position = v.position;
        normal = v.normal;
    } else if (surface_data.p[0].x == 7.0) {
        position.y *= surface_data.p[1].y;
    } else if (surface_data.p[0].x == 1.0 && v.position.z < 2.0) {
        let sample = surface_sweep_sample(i32(v.position.z), v.position.x, v.position.y);
        position = sample.position;
        normal = sv_unit(sample.normal);
        derive = sv_dot(sample.normal, sample.normal) < 0.000000000001;
    } else if (surface_data.p[0].x != 0.0) {
        position = surface_position(i32(v.position.z), v.position.x, v.position.y);
        derive = true;
    }

    if (derive) {
        normal = surface_normal(i32(v.position.z), v.position.x, v.position.y);
    }
    return place_at(v, position, normal);
}
