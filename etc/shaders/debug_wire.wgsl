// Wireframe of the debug pass of a view (ViewDebug.wireframe): the triangles of the mesh draws of
// the view, drawn with the indirect arguments and visible slots its cull pass wrote, as its
// depth prepass draws them (group 2 is the instance group without the shading stream).
//
// It needs no line polygon mode: the draw binds a shared index buffer of the sequence 0, 1, 2,
// ..., so vertex_index is the position in the index list of the mesh. The vertex stage loads the
// index from wire_indices and the vertex from wire_vertices (box_vertex_t, eight floats) and
// places it with place() of place.wgsl, as mesh.wgsl does, so a parametric surface lies where it
// is shaded. The corner of a vertex in its triangle, vertex_index % 3, gives its barycentric
// coordinates; the fragment stage turns the distance to the nearest edge into pixels with
// fwidth and covers the pixels within the width of the line, anti aliased over one pixel.
//
// wire_adjacency holds, for each triangle and each of its edges (the edge opposite corner k is
// where bary[k] is 0), the vertex of the neighbor triangle across the edge that is not on it, or
// WIRE_NONE on a border, from edges matched by position on the CPU. The vertex stage places the
// three corners of its triangle and the opposite vertex of each neighbor, and marks an edge one
// sided when it has no neighbor or the neighbor faces the other way (a silhouette).
//
// The output is premultiplied and linear, the color of the uniform is decoded on the CPU.
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

@group(3) @binding(0) var<uniform> wire: Wire;

struct WireOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) bary: vec3f,
    // 1 for each edge whose line is drawn on the side of this triangle only
    @location(1) @interpolate(flat) single: vec3u,
};

// Place a vertex of the mesh for an instance slot.
fn wire_place(index: u32, slot: u32) -> Placed {
    let base = index * WIRE_VERTEX_FLOATS;
    var v: VertexIn;
    v.vertex = index;
    v.position = vec3f(wire_vertices[base], wire_vertices[base + 1u], wire_vertices[base + 2u]);
    v.normal = vec3f(wire_vertices[base + 3u], wire_vertices[base + 4u],
        wire_vertices[base + 5u]);
    v.uv = vec2f(wire_vertices[base + 6u], wire_vertices[base + 7u]);
    v.slot = slot;
    return place(v);
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

@vertex fn vs(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    let corner = i % 3u;
    let first = i - corner;
    var world: array<vec4f, 3>;
    var o: WireOut;
    for (var k = 0u; k < 3u; k++) {
        let placed = wire_place(wire_indices[first + k], slot);
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
        let n = wire_place(opposite, slot).world;
        single[k] = select(0u, 1u, wire_front(q, p, n) != front);
    }

    o.bary = vec3f(select(0.0, 1.0, corner == 0u), select(0.0, 1.0, corner == 1u),
        select(0.0, 1.0, corner == 2u));
    o.single = single;
    return o;
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
