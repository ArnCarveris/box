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

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(0) var<storage, read> wire_vertices: array<f32>;
@group(1) @binding(1) var<storage, read> wire_indices: array<u32>;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(3) var<storage, read> surfaces: array<SurfaceData>;

@group(3) @binding(0) var<uniform> wire: Wire;

struct WireOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) bary: vec3f,
};

@vertex fn vs(@builtin(vertex_index) i: u32, @location(0) slot: u32) -> WireOut {
    let index = wire_indices[i];
    let base = index * WIRE_VERTEX_FLOATS;
    var v: VertexIn;
    v.vertex = index;
    v.position = vec3f(wire_vertices[base], wire_vertices[base + 1u], wire_vertices[base + 2u]);
    v.normal = vec3f(wire_vertices[base + 3u], wire_vertices[base + 4u],
        wire_vertices[base + 5u]);
    v.uv = vec2f(wire_vertices[base + 6u], wire_vertices[base + 7u]);
    v.slot = slot;
    let corner = i % 3u;
    var o: WireOut;
    o.pos = place(v).pos;
    o.bary = vec3f(select(0.0, 1.0, corner == 0u), select(0.0, 1.0, corner == 1u),
        select(0.0, 1.0, corner == 2u));
    return o;
}

@fragment fn fs(f: WireOut) -> @location(0) vec4f {
    let width = wire.params.x;
    let b = f.bary;
    let d = b / max(fwidth(b), vec3f(1e-5));
    let edge = min(min(d.x, d.y), d.z);
    let a = 1.0 - smoothstep(width - 1.0, width, edge);
    let alpha = wire.color.a * a;
    if (alpha <= 0.0) {
        discard;
    }
    return vec4f(wire.color.rgb * alpha, alpha);
}
