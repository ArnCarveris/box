// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only. The cull pass writes the slots of the casters inside the cascade, which step per
// instance; vs only reads the transform stream and the surface stream of the instance page
// (group 1 without the shading stream).
// Alpha tested casters (AlphaCutoff) draw with vs_cutout and fs_cutout, which also bind the
// shading stream and the material table, read the alpha of the instance and the UV transform and
// the cutoff of its material, and discard where the alpha times the albedo alpha of the batch
// (group 2) is below the cutoff, as the forward pass does. A MappingObject or MappingWorld
// material projects the UV in the fragment stage, as mesh.wgsl does (mapping_uv), and with a
// blend above 0 blends the alpha of its three axis projections (mapping_alpha). Alpha tested
// casters with a TextureBlend draw with fs_cutout_blend and fs_point_cutout_blend, which bind the
// blend group of their batch in group 2 instead and discard where the alpha times the weighted
// alpha of the layer albedos is below the cutoff (blend_alpha), so they cast the shadow they
// show.
//
// Spot light tiles of the shadow atlas draw with vs and vs_cutout too. A point light tile is
// drawn once per octant of its octahedron with vs_point and vs_point_cutout, the octant in the
// uniforms (Praun and Hoppe, as Esoterica's octahedral shadows): each octant is a perspective
// along the axis signs / sqrt(3) with fixed signs, and a triangle is kept to its octant by 3
// half spaces. With BOX_SHADOW_CLIP_DISTANCES (the device has clip distances) they are clip
// distances; without, fs_point and fs_point_cutout discard where one is negative.
//
// An atlas pass that keeps the tiles that did not change loads the atlas and clears each tile it
// renders with vs_clear: a triangle over the viewport of the tile at depth 0, the reversed Z
// clear value, drawn with depth compare always and no bind groups.
#ifdef BOX_SHADOW_CLIP_DISTANCES
enable clip_distances;
#endif

#include "include/instance.wgsl"
#include "include/material.wgsl"
#include "include/blend.wgsl"

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

// The blend group of an alpha tested caster with a TextureBlend (fs_cutout_blend and
// fs_point_cutout_blend only): the albedo of each layer as a slice of an array, the mask, the
// uniforms and the heights, at the bindings mesh.wgsl has them.
@group(2) @binding(5) var blend_albedo: texture_2d_array<f32>;
@group(2) @binding(8) var blend_mask: texture_2d<f32>;
@group(2) @binding(9) var<uniform> blending: BlendUniforms;
@group(2) @binding(10) var blend_heights: texture_2d_array<f32>;

struct VertexIn {
    @location(0) position: vec3f,
    @location(3) slot: u32,
};

struct CutoutIn {
    @builtin(vertex_index) vertex: u32,
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) slot: u32,
};

// The UV of an alpha tested vertex, or for a MappingObject or MappingWorld material (mapped 1
// plus its blend) the offset of its projection, with the object (or world) position and normal
// mapping_uv projects, as mesh.wgsl has them.
struct CutoutOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) alpha: f32,
    @location(2) @interpolate(flat) cutoff: f32,
    @location(3) @interpolate(flat) mapped: f32,
    @location(4) object: vec4f,
    @location(5) object_normal: vec3f,
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
    @location(3) @interpolate(flat) mapped: f32,
    @location(4) object: vec4f,
    @location(5) object_normal: vec3f,
#ifdef BOX_SHADOW_CLIP_DISTANCES
    @builtin(clip_distances) clip: array<f32, 3>,
#else
    @location(6) clip: vec3f,
#endif
};

// World position of a vertex of a caster; sets surface_data to the surface of its instance. A
// plain mesh page binds one zero record as its surface stream, so the record is loaded only for a
// slot inside the buffer and surface_data stays zero otherwise, the plain mesh path.
fn caster_world(slot: u32, vertex: vec3f) -> vec3f {
    if (slot < arrayLength(&surfaces)) {
        surface_data = surfaces[slot];
    }
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

@vertex fn vs_clear(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(corner * 2.0 - 1.0, 0.0, 1.0);
}

@vertex fn vs(v: VertexIn) -> @builtin(position) vec4f {
    return caster_position(v.slot, v.position);
}

// The alpha tested attributes of a vertex: the UV as vs of mesh.wgsl transforms it (the side
// region of a kind 7 surface, whose UVs start two tiles over, moves back, and a MappingObject
// material on a Sweep takes lengths over its size), or the projection of
// a MappingObject or MappingWorld material on a plain mesh, the alpha of the instance and the
// cutoff of its material. Reads surface_data, so the position comes first.
fn cutout_attributes(v: CutoutIn, o: ptr<function, CutoutOut>) {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material);
    (*o).uv = v.uv * material.uv.xy + material.uv.zw;
    if (surface_data.p[0].x == 7.0 && v.uv.x >= 2.0 &&
        !(v.normal.x == 0.0 && v.normal.z == 0.0 && v.normal.y != 0.0)) {
        (*o).uv.x -= 2.0 * material.uv.x;
    }
    if (material_mapped(material) && !material_world(material) && surface_data.p[0].x == 1.0) {
        (*o).uv = surface_sweep_lengths(v.uv, v.position, v.vertex) / material.mapping_size *
            material.uv.xy + material.uv.zw;
    }
    (*o).mapped = 0.0;
    (*o).object = vec4f(0.0);
    (*o).object_normal = vec3f(0.0);
    if (material_mapped(material) && surface_data.p[0].x == 0.0) {
        let model = instance_model(transforms[v.slot]);
        let c0 = model[0].xyz;
        let c1 = model[1].xyz;
        let c2 = model[2].xyz;
        let scale = vec3f(length(c0), length(c1), length(c2));
        (*o).mapped = 1.0 + material_blend(material);
        (*o).uv = mapping_offset(material);
        (*o).object = mapping_object(material, v.position, scale);
        (*o).object_normal = v.normal;
        if (material_world(material)) {
            let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
            (*o).object = mapping_object(material, (model * vec4f(v.position, 1.0)).xyz,
                vec3f(1.0));
            (*o).object_normal = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1)) *
                v.normal * flip;
        }
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

// Discard where the alpha times the albedo alpha of the batch is below the cutoff: at the
// interpolated UV, or projected for a MappingObject or MappingWorld material (mapped not 0),
// blended over the three axes with a blend (mapped above 1), as mapping_alpha makes it.
fn cutout_test(uv: vec2f, alpha: f32, cutoff: f32, mapped: f32, object: vec4f,
    object_normal: vec3f) {
    if (alpha * mapping_alpha(albedo_map, material_sampler, uv, mapped, object, object_normal) <
        cutoff) {
        discard;
    }
}

@fragment fn fs_cutout(f: CutoutOut) {
    cutout_test(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
}

// Discard as cutout_test does, with the weighted alpha of the layers of a TextureBlend in place
// of the albedo alpha of the batch.
fn cutout_test_blend(uv: vec2f, alpha: f32, cutoff: f32, mapped: f32, object: vec4f,
    object_normal: vec3f) {
    if (alpha * blend_alpha(blend_albedo, blend_mask, blend_heights, material_sampler, blending,
        uv, mapped, object, object_normal) < cutoff)
    {
        discard;
    }
}

@fragment fn fs_cutout_blend(f: CutoutOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
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
    o.mapped = c.mapped;
    o.object = c.object;
    o.object_normal = c.object_normal;
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
    cutout_test(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
}

@fragment fn fs_point_cutout_blend(f: CutoutOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
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
    cutout_test(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
    octant_discard(f.clip);
}

@fragment fn fs_point_cutout_blend(f: PointCutoutOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
    octant_discard(f.clip);
}
#endif
