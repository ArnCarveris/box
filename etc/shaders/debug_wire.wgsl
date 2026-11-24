// Wireframe of the debug pass of a view (ViewDebug.wireframe): the triangles of the mesh draws of
// the view, drawn with the indirect arguments and visible slots its cull pass wrote, as its
// depth prepass draws them (group 2 is the instance group without the shading stream). The
// batches with a WindSway draw with vs_wind, whose group 2 has the shading stream and the
// material table, and move the vertices by the wind as the mesh shaders do; those whose mesh has
// per vertex data with vs_wind_data, whose group 1 has the data too, which it reads for the
// pivots of a WindSway as the _data entry points of the mesh shaders do.
#include "include/lights.wgsl"
#include "include/place.wgsl"

struct Wire {
    // Linear color, straight alpha
    color: vec4f,
    // x: width of the lines in pixels, yzw unused
    params: vec4f,
};

// Floats of a box_vertex_t: position, normal and uv.
const WIRE_VERTEX_FLOATS: u32 = 8u;

// Neighbor of an edge on the border of the mesh
const WIRE_NONE: u32 = 0xFFFFFFFFu;

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(0) var<storage, read> wire_vertices: array<f32>;
// The indices of the mesh, then the edge adjacency: one neighbor per index.
@group(1) @binding(1) var<storage, read> wire_edges: array<u32>;
// Read by vs_wind_data only: data0 to data3 of every vertex (VertexData).
@group(1) @binding(2) var<storage, read> wire_data: array<vec4f>;

// vec4 of per vertex data per vertex (BOX_RENDER_DATA_VECS).
const WIRE_DATA_VECS: u32 = 4u;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(6) var<uniform> instance_page: InstancePage;
@group(2) @binding(3) var<storage, read> surfaces: array<SurfaceData>;
// Read by vs_wind only, which the pipelines of the batches with a WindSway use.
@group(2) @binding(2) var<storage, read> shadings: array<MeshShading>;
@group(2) @binding(4) var<storage, read> materials: array<Material>;

@group(3) @binding(0) var<uniform> wire: Wire;

struct WireOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) bary: vec3f,
    // 1 for each edge whose line is drawn on the side of this triangle only
    @location(1) @interpolate(flat) single: vec3u,
};

// m: the material of the instance, moving the vertex by the wind with wind.
fn wire_place(index: u32, slot: u32, m: Material, wind: bool) -> Placed {
    let base = index * WIRE_VERTEX_FLOATS;
    var v: VertexIn;
    v.vertex = index;
    v.position = vec3f(wire_vertices[base], wire_vertices[base + 1u], wire_vertices[base + 2u]);
    v.normal = vec3f(wire_vertices[base + 3u], wire_vertices[base + 4u],
        wire_vertices[base + 5u]);
    v.uv = vec2f(wire_vertices[base + 6u], wire_vertices[base + 7u]);
    v.slot = slot;
    let p = place(v);
    if (wind && wind_sways(m)) {
        return place_wind(p, m);
    }
    return p;
}

// wire_place of a vertex of a swaying mesh with per vertex data, which the pivots of a WindSway
// come from, as the _data entry points of the mesh shaders read them.
fn wire_place_data(index: u32, slot: u32, m: Material) -> Placed {
    let d = index * WIRE_DATA_VECS;
    wind_vertex_data(wire_data[d], wire_data[d + 2u], wire_data[d + 3u]);
    return wire_place(index, slot, m, true);
}

// Whether the triangle a, b, c of world positions faces the camera.
fn wire_front(a: vec4f, b: vec4f, c: vec4f) -> bool {
    let pa = (view.view * a).xyz;
    let pb = (view.view * b).xyz;
    let pc = (view.view * c).xyz;
    let n = cross(pb - pa, pc - pa);
    let facing = select(dot(n, -pa), n.z, view.lights.w == 1u);
    return facing > 0.0;
}

// The corners of the triangle of the wire vertex i, then the third corner of the neighbor
// across each of its edges, which runs along the edge the other way (from the third corner to
// the second), WIRE_NONE on the border of the mesh.
fn wire_corners(i: u32) -> array<u32, 6> {
    let first = i - i % 3u;
    let count = arrayLength(&wire_edges) / 2u;
    var result: array<u32, 6>;
    for (var k = 0u; k < 3u; k++) {
        result[k] = wire_edges[first + k];
        result[k + 3u] = wire_edges[count + first + k];
    }
    return result;
}

// The wire vertex i from the corners of wire_corners, the world positions they were placed at
// and the clip position of the corner of i.
fn wire_vertex(i: u32, corners: array<u32, 6>, placed: array<vec4f, 6>, pos: vec4f) -> WireOut {
    var c = corners;
    var world = placed;
    let corner = i % 3u;
    var o: WireOut;
    o.pos = pos;
    let front = wire_front(world[0], world[1], world[2]);
    var single = vec3u(0u);
    for (var k = 0u; k < 3u; k++) {
        if (c[k + 3u] == WIRE_NONE) {
            single[k] = 1u;
            continue;
        }

        let p = world[(k + 1u) % 3u];
        let q = world[(k + 2u) % 3u];
        single[k] = select(0u, 1u, wire_front(q, p, world[k + 3u]) != front);
    }

    o.bary = vec3f(select(0.0, 1.0, corner == 0u), select(0.0, 1.0, corner == 1u),
        select(0.0, 1.0, corner == 2u));
    o.single = single;
    return o;
}

@vertex fn vs(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    var m: Material;
    var c = wire_corners(i);
    var world: array<vec4f, 6>;
    var pos = vec4f(0.0);
    for (var k = 0u; k < 6u; k++) {
        if (c[k] != WIRE_NONE) {
            let p = wire_place(c[k], slot, m, false);
            world[k] = p.world;
            pos = select(pos, p.pos, k == i % 3u);
        }
    }
    return wire_vertex(i, c, world, pos);
}

@vertex fn vs_wind(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    let m = material_fetch(shadings[slot_of(slot)].material & 0xFFFFFFu);
    var c = wire_corners(i);
    var world: array<vec4f, 6>;
    var pos = vec4f(0.0);
    for (var k = 0u; k < 6u; k++) {
        if (c[k] != WIRE_NONE) {
            let p = wire_place(c[k], slot, m, true);
            world[k] = p.world;
            pos = select(pos, p.pos, k == i % 3u);
        }
    }
    return wire_vertex(i, c, world, pos);
}

// The wire draws of the swaying meshes with per vertex data, which a WindSway with pivots reads.
@vertex fn vs_wind_data(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    let m = material_fetch(shadings[slot_of(slot)].material & 0xFFFFFFu);
    var c = wire_corners(i);
    var world: array<vec4f, 6>;
    var pos = vec4f(0.0);
    for (var k = 0u; k < 6u; k++) {
        if (c[k] != WIRE_NONE) {
            let p = wire_place_data(c[k], slot, m);
            world[k] = p.world;
            pos = select(pos, p.pos, k == i % 3u);
        }
    }
    return wire_vertex(i, c, world, pos);
}

// A triangle covers its side of an edge up to width - 0.5 pixels from it. The line of an edge
// between two triangles that face the same way is drawn half on each side, so each covers half
// the width plus half a pixel; an edge with one side, on the border of an open mesh or on the
// silhouette of a closed one, covers the whole width on its side. Every line has the width of
// the view.
@fragment fn fs(f: WireOut) -> @location(0) vec4f {
    let half = wire.params.x * 0.5;
    let width = vec3f(half + 0.5) + vec3f(f.single) * half;
    let b = f.bary;
    let d = b / max(fwidth(b), vec3f(1e-5));
    let a = 1.0 - smoothstep(width - 1.0, width, d);
    let coverage = max(max(a.x, a.y), a.z);
    let alpha = wire.color.a * coverage;
    if (alpha <= 0.0) {
        discard;
    }
    return vec4f(wire.color.rgb * alpha, alpha);
}
