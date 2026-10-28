// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The cull pass writes the slots of the casters inside the cascade, which step per
// instance; vs only reads the transform stream and the surface stream of the instance page
// (group 1 without the shading stream).
// Alpha tested casters (AlphaCutoff) draw with vs_cutout and fs_cutout, which also bind the
// shading stream and the material table, read the alpha of the instance and the UV transform and
// the cutoff of its material, and discard where the alpha times the albedo alpha of the batch
// (group 2) is below the cutoff, as the forward pass does.
//
// Spot light tiles of the shadow atlas draw with vs and vs_cutout too. A point light tile is
// drawn once per octant of its octahedron with vs_point and vs_point_cutout, the octant in the
// uniforms (Praun and Hoppe, as Esoterica's octahedral shadows): each octant is a perspective
// along the axis signs / sqrt(3) with fixed signs, and a triangle is kept to its octant by 3
// half spaces. With BOX_SHADOW_CLIP_DISTANCES (the device has clip distances) they are clip
// distances; without, fs_point and fs_point_cutout discard where one is negative.
#ifdef BOX_SHADOW_CLIP_DISTANCES
enable clip_distances;
#endif

#include "include/instance.wgsl"
#include "include/material.wgsl"

// Uniforms of a cascade, a spot light tile or an octant of a point light tile.
struct Cascade {
    // World to clip space of a cascade or a spot light tile
    matrix: mat4x4f,
    // A point light tile: xyz the world position of the light, w the octant, 0 to 7
    light: vec4f,
    // A point light tile: x the near plane, y the far plane
    planes: vec4f,
};

// 1 / sqrt(3), the length of the axis of an octant of a point light tile
const OCTANT_AXIS: f32 = 0.57735026919;

@group(0) @binding(0) var<uniform> cascade: Cascade;
@group(1) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(1) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(1) @binding(2) var<storage, read> shadings: array<MeshShading>;
@group(1) @binding(3) var<storage, read> surfaces: array<SurfaceData>;
@group(1) @binding(4) var<storage, read> materials: array<Material>;

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

// A vertex of a caster in an octant of a point light tile, with the 3 half space distances of
// the octant.
struct PointOut {
    @builtin(position) pos: vec4f,
#ifdef BOX_SHADOW_CLIP_DISTANCES
    @builtin(clip_distances) clip: array<f32, 3>,
#else
    @location(0) clip: vec3f,
#endif
};

// An alpha tested vertex of a caster in an octant of a point light tile.
struct PointCutoutOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) alpha: f32,
    @location(2) @interpolate(flat) cutoff: f32,
#ifdef BOX_SHADOW_CLIP_DISTANCES
    @builtin(clip_distances) clip: array<f32, 3>,
#else
    @location(3) clip: vec3f,
#endif
};

// World position of a vertex of a caster; sets surface_data to the surface of its instance.
fn caster_world(slot: u32, vertex: vec3f) -> vec3f {
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
    return (model * vec4f(position, 1.0)).xyz;
}

// Position of a vertex of a caster in the clip space of the cascade.
fn caster_position(slot: u32, vertex: vec3f) -> vec4f {
    return cascade.matrix * vec4f(caster_world(slot, vertex), 1.0);
}

// Axis signs of an octant: bit 0 negates x, bit 1 y and bit 2 z.
fn octant_signs(octant: u32) -> vec3f {
    let bits = vec3u(octant) & vec3u(1u, 2u, 4u);
    return select(vec3f(1.0), vec3f(-1.0), bits != vec3u(0u));
}

// Clip position of a world position in the octant of the uniforms (ProjectOctahedralShadow-
// FacePosition of Esoterica): x, y the octahedral unfolding, the octants with negative z folded
// out to the corners of the square, y negated to the framebuffer orientation; w the distance
// along the axis of the octant and reversed Z depth near * (far - w) / (w * (far - near)).
fn octant_position(world: vec3f) -> vec4f {
    let signs = octant_signs(u32(cascade.light.w));
    let v = world - cascade.light.xyz;
    let fold = select(0.0, 1.0, signs.z < 0.0);
    let near_ = cascade.planes.x;
    let far_ = cascade.planes.y;
    let scale = near_ / (far_ - near_);
    let offset = near_ * far_ / (far_ - near_);
    let axis = OCTANT_AXIS * dot(signs, v);
    return vec4f(
        OCTANT_AXIS * (v.x + fold * signs.x * signs.z * v.z),
        -OCTANT_AXIS * (v.y + fold * signs.y * signs.z * v.z),
        offset - scale * axis,
        axis);
}

// Distances of a world position to the 3 half spaces of the octant of the uniforms, all non
// negative inside it.
fn octant_distances(world: vec3f) -> vec3f {
    return octant_signs(u32(cascade.light.w)) * (world - cascade.light.xyz);
}

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    return caster_position(v.slot, v.position);
}

// The alpha tested attributes of a vertex: the UV as vs of mesh.wgsl transforms it (the side
// region of a kind 7 surface, whose UVs start two tiles over, moves back), the alpha of the
// instance and the cutoff of its material. Reads surface_data, so the position comes first.
fn cutout_attributes(v: CutoutIn, o: ptr<function, CutoutOut>) {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material);
    (*o).uv = v.uv * material.uv.xy + material.uv.zw;
    if (surface_data.p[0].x == 7.0 && v.uv.x >= 2.0 && abs(v.normal.y) <= 0.5) {
        (*o).uv.x -= 2.0 * material.uv.x;
    }
    (*o).alpha = shading.alpha;
    (*o).cutoff = material.cutoff;
}

@vertex fn vs_cutout(v: CutoutIn) -> CutoutOut {
    var o: CutoutOut;
    o.pos = caster_position(v.slot, v.position);
    cutout_attributes(v, &o);
    return o;
}

// Discard where the alpha times the albedo alpha of the batch is below the cutoff.
fn cutout_test(uv: vec2f, alpha: f32, cutoff: f32) {
    if (alpha * textureSample(albedo_map, material_sampler, uv).a < cutoff) {
        discard;
    }
}

@fragment fn fs_cutout(f: CutoutOut) {
    cutout_test(f.uv, f.alpha, f.cutoff);
}

@vertex fn vs_point(v: VertexIn) -> PointOut {
    let world = caster_world(v.slot, v.position);
    let d = octant_distances(world);
    var o: PointOut;
    o.pos = octant_position(world);
#ifdef BOX_SHADOW_CLIP_DISTANCES
    o.clip = array<f32, 3>(d.x, d.y, d.z);
#else
    o.clip = d;
#endif
    return o;
}

@vertex fn vs_point_cutout(v: CutoutIn) -> PointCutoutOut {
    let world = caster_world(v.slot, v.position);
    let d = octant_distances(world);
    var c: CutoutOut;
    cutout_attributes(v, &c);
    var o: PointCutoutOut;
    o.pos = octant_position(world);
    o.uv = c.uv;
    o.alpha = c.alpha;
    o.cutoff = c.cutoff;
#ifdef BOX_SHADOW_CLIP_DISTANCES
    o.clip = array<f32, 3>(d.x, d.y, d.z);
#else
    o.clip = d;
#endif
    return o;
}

#ifdef BOX_SHADOW_CLIP_DISTANCES
// The clip distances already kept the fragment to its octant.
@fragment fn fs_point_cutout(f: CutoutOut) {
    cutout_test(f.uv, f.alpha, f.cutoff);
}
#else
// Discard a fragment outside the octant of its draw, which core WebGPU cannot clip.
fn octant_discard(clip: vec3f) {
    if (any(clip < vec3f(0.0))) {
        discard;
    }
}

@fragment fn fs_point(f: PointOut) {
    octant_discard(f.clip);
}

@fragment fn fs_point_cutout(f: PointCutoutOut) {
    cutout_test(f.uv, f.alpha, f.cutoff);
    octant_discard(f.clip);
}
#endif
