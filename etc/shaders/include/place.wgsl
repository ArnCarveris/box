// Placement of the vertices of a mesh instance, shared by the vertex stages of mesh.wgsl and
// debug_wire.wgsl. The includer declares view (group 0) and the transforms, surface_assets and
// surfaces streams and the instance_page of the instance group (group 2, bindings 0, 1, 3 and
// 6); the entry points that move vertices by the wind (place_wind) also the material table
// (binding 4). A vertex is shaded at its local position and its clip position is placed from
// its position relative to the render origin, which keeps the precision near it.
#pragma once
#include "instance.wgsl"
#include "material.wgsl"
#include "wind.wgsl"
#include "draw_range.wgsl"

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
    // The local position, which the shading takes
    world: vec4f,
    // The clip position: render_view_proj of the position relative to the render origin
    pos: vec4f,
    // The object position, the evaluated position of a parametric surface
    local: vec3f,
    // Derivative along world Y of the world offset a WindSway moved the vertex by, which bends
    // the normal; 0 for a vertex the wind does not move
    slope: vec3f,
    // Turn of the branch of the vertex about its pivot by a WindSway with pivots (WindMove.turn),
    // a unit quaternion which turns the world normal before the slope bends it; the identity
    // without one
    turn: vec4f,
    // The position relative to the render origin, which pos projects
    relative: vec4f,
};

fn place_at(v: VertexIn, position: vec3f, normal: vec3f) -> Placed {
    let t = transforms[slot_of(v.slot)];
    let model = instance_model(instance_local(t, instance_page));
    let world = model * vec4f(position, 1.0);
    let relative = instance_model(instance_relative(t, instance_page)) * vec4f(position, 1.0);
    return Placed(model, normal, world, view.render_view_proj * relative, position, vec3f(0.0),
        vec4f(0.0, 0.0, 0.0, 1.0), relative);
}

// A placed vertex moved by the wind (wind.wgsl): its world and clip position move, the origin of
// its shape is the translation of its model matrix.
fn place_wind(p: Placed, m: Material) -> Placed {
    var result = p;
    let w = wind_move(p.world.xyz, p.model, m, world_origin(view.origin));
    result.world = vec4f(p.world.xyz + w.offset, p.world.w);
    result.relative = vec4f(p.relative.xyz + w.offset, p.relative.w);
    result.pos = view.render_view_proj * result.relative;
    result.slope = w.slope;
    result.turn = w.turn;
    return result;
}

fn place_mesh(v: VertexIn) -> Placed {
    return place_at(v, v.position, v.normal);
}

fn place(v: VertexIn) -> Placed {
    if (slot_of(v.slot) < arrayLength(&surfaces)) {
        surface_data = surfaces[slot_of(v.slot)];
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
    } else if (surface_data.p[0].x == TERRAIN_SURFACE) {
        let t = terrain_vertex(v.position);
        position = t.position;
        normal = t.normal;
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
