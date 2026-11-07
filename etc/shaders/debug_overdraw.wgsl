#include "include/lights.wgsl"
#include "include/place.wgsl"
#include "include/fullscreen.wgsl"

const OVERDRAW_VERTEX_FLOATS: u32 = 8u;

const OVERDRAW_STOPS: u32 = 9u;

@group(0) @binding(0) var<uniform> view: View;

@group(1) @binding(0) var<storage, read> overdraw_vertices: array<f32>;
@group(1) @binding(1) var<storage, read> overdraw_indices: array<u32>;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(3) var<storage, read> surfaces: array<SurfaceData>;

@group(1) @binding(3) var overdraw_count: texture_2d<f32>;

@vertex fn vs_count(@builtin(vertex_index) i: u32, @location(0) slot: u32)
    -> @builtin(position) @invariant vec4f
{
    let index = overdraw_indices[i];
    let base = index * OVERDRAW_VERTEX_FLOATS;
    var v: VertexIn;
    v.vertex = index;
    v.position = vec3f(overdraw_vertices[base], overdraw_vertices[base + 1u],
        overdraw_vertices[base + 2u]);
    v.normal = vec3f(overdraw_vertices[base + 3u], overdraw_vertices[base + 4u],
        overdraw_vertices[base + 5u]);
    v.uv = vec2f(overdraw_vertices[base + 6u], overdraw_vertices[base + 7u]);
    v.slot = slot;
    return place(v).pos;
}

@fragment fn fs_count() -> @location(0) vec4f {
    return vec4f(1.0, 0.0, 0.0, 0.0);
}

fn overdraw_linear(c: vec3f) -> vec3f {
    return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

fn overdraw_ramp(layers: f32) -> vec3f {
    var stops = array<vec3f, OVERDRAW_STOPS>(
        vec3f(0.0, 0.0, 0.0),
        vec3f(0.05, 0.1, 0.45),
        vec3f(0.0, 0.45, 0.85),
        vec3f(0.0, 0.75, 0.45),
        vec3f(0.55, 0.85, 0.0),
        vec3f(1.0, 0.85, 0.0),
        vec3f(1.0, 0.5, 0.0),
        vec3f(0.9, 0.1, 0.1),
        vec3f(1.0, 1.0, 1.0)
    );
    let n = u32(clamp(round(layers), 0.0, f32(OVERDRAW_STOPS - 1u)));
    return overdraw_linear(stops[n]);
}

@fragment fn fs(f: FullscreenOut) -> @location(0) vec4f {
    let layers = textureLoad(overdraw_count, vec2i(f.pos.xy), 0).r;
    return vec4f(overdraw_ramp(layers), 1.0);
}
