// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The cull pass writes the slots of the casters inside the cascade, which step per
// instance; vs only reads the transform stream and the surface stream of the instance page
// (group 1 without the shading stream).
// Alpha tested casters (AlphaCutoff) draw with vs_cutout and fs_cutout, which also bind the
// shading stream and read the UV, the UV transform, the alpha and the cutoff of the instance and
// discard where the alpha times the albedo alpha of the batch (group 2) is below the cutoff, as
// the forward pass does.
#include "include/instance.wgsl"

struct Cascade {
    matrix: mat4x4f,
};

@group(0) @binding(0) var<uniform> cascade: Cascade;
@group(1) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(1) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(1) @binding(2) var<storage, read> shadings: array<InstanceShading>;
@group(1) @binding(3) var<storage, read> surfaces: array<SurfaceData>;

@group(2) @binding(0) var albedo_map: texture_2d<f32>;
@group(2) @binding(4) var material_sampler: sampler;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) slot: u32,
};

struct CutoutIn {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) slot: u32,
};

struct CutoutOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) alpha: f32,
    @location(2) @interpolate(flat) cutoff: f32,
};

// Position of a vertex of a caster in the clip space of the cascade.
fn caster_position(slot: u32, vertex: vec3f) -> vec4f {
    surface_data = surfaces[slot];
    let model = instance_model(transforms[slot]);
    var position = vertex;
    if (surface_data.p[19].x != 0.0) {
        position = vertex;
    } else if (surface_data.p[0].x == 7.0) {
        position.y *= surface_data.p[1].y;
    } else if (surface_data.p[0].x != 0.0) {
        position = surface_position(i32(position.z), position.x, position.y);
    }
    return cascade.matrix * (model * vec4f(position, 1.0));
}

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    return caster_position(v.slot, v.position);
}

// The UV as vs of mesh.wgsl transforms it: the side region of a kind 7 surface, whose UVs start
// two tiles over, moves back.
@vertex fn vs_cutout(v: CutoutIn) -> CutoutOut {
    let shading = shadings[v.slot];
    var o: CutoutOut;
    o.pos = caster_position(v.slot, v.position);
    o.uv = v.uv * shading.uv.xy + shading.uv.zw;
    if (surface_data.p[0].x == 7.0 && v.uv.x >= 2.0 && abs(v.normal.y) <= 0.5) {
        o.uv.x -= 2.0 * shading.uv.x;
    }
    o.alpha = shading.color.a;
    o.cutoff = shading.material.z;
    return o;
}

@fragment fn fs_cutout(f: CutoutOut) {
    let alpha = f.alpha * textureSample(albedo_map, material_sampler, f.uv).a;
    if (alpha < f.cutoff) {
        discard;
    }
}
