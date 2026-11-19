// Wireframe of the debug pass of a view (ViewDebug.wireframe): the triangles of the mesh draws of
// the view, drawn with the indirect arguments and visible slots its cull pass wrote, as its
// depth prepass draws them (group 2 is the instance group without the shading stream). The
// batches with a WindSway draw with vs_wind, whose group 2 has the shading stream and the
// material table, and move the vertices by the wind as the mesh shaders do.
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
@group(1) @binding(1) var<storage, read> wire_indices: array<u32>;
@group(1) @binding(2) var<storage, read> wire_adjacency: array<u32>;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
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

// Whether the triangle a, b, c of world positions faces the camera.
fn wire_front(a: vec4f, b: vec4f, c: vec4f) -> bool {
    let pa = (view.view * a).xyz;
    let pb = (view.view * b).xyz;
    let pc = (view.view * c).xyz;
    let n = cross(pb - pa, pc - pa);
    let facing = select(dot(n, -pa), n.z, view.lights.w == 1u);
    return facing > 0.0;
}

fn wire_vertex(i: u32, slot: u32, m: Material, wind: bool) -> WireOut {
    let corner = i % 3u;
    let first = i - corner;
    var world: array<vec4f, 3>;
    var o: WireOut;
    for (var k = 0u; k < 3u; k++) {
        let placed = wire_place(wire_indices[first + k], slot, m, wind);
        world[k] = placed.world;
        if (k == corner) {
            o.pos = placed.pos;
        }
    }

    let front = wire_front(world[0], world[1], world[2]);
    var single = vec3u(0u);
    for (var k = 0u; k < 3u; k++) {
        let opposite = wire_adjacency[first + k];
        if (opposite == WIRE_NONE) {
            single[k] = 1u;
            continue;
        }

        // The neighbor runs along the edge the other way: from the third corner to the second
        let p = world[(k + 1u) % 3u];
        let q = world[(k + 2u) % 3u];
        let n = wire_place(opposite, slot, m, wind).world;
        single[k] = select(0u, 1u, wire_front(q, p, n) != front);
    }

    o.bary = vec3f(select(0.0, 1.0, corner == 0u), select(0.0, 1.0, corner == 1u),
        select(0.0, 1.0, corner == 2u));
    o.single = single;
    return o;
}

@vertex fn vs(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    var m: Material;
    return wire_vertex(i, slot, m, false);
}

@vertex fn vs_wind(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    let m = material_fetch(shadings[slot_of(slot)].material & 0xFFFFFFu);
    return wire_vertex(i, slot, m, true);
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
