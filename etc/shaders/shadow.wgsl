// Shadow pass: draws the shadow casters of a view into one cascade of its shadow map, depth
// only.
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
// Transforms relative to the texture frame, read by MappingFrame materials only: the frame pages
// have the stream, the other pages bind one zero record.
@group(1) @binding(5) var<storage, read> frames: array<InstanceTransform>;

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
    @builtin(vertex_index) vertex: u32,
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) slot: u32,
};

// The UV of a vertex, or for a MappingObject, MappingWorld or MappingFrame material (mapped 1 plus
// its blend) the offset of its projection, with the object (world, frame) position and normal
// mapping_uv projects, as mesh.wgsl has them.
struct CasterOut {
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

struct ShadowOut {
    @builtin(position) pos: vec4f,
};

struct PointShadowOut {
    @builtin(position) pos: vec4f,
#ifdef BOX_SHADOW_CLIP_DISTANCES
    @builtin(clip_distances) clip: array<f32, 3>,
#else
    @location(6) clip: vec3f,
#endif
};

fn caster_world_mesh(slot: u32, vertex: vec3f) -> vec3f {
    return (instance_model(transforms[slot]) * vec4f(vertex, 1.0)).xyz;
}

// World position of a vertex of a caster; sets surface_data. A plain mesh page binds one zero
// record as its surface stream, so a slot past it leaves surface_data zero (the plain mesh path).
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

// Object position and normal of a vertex of a parametric surface caster, as place of place.wgsl
// derives them, for the projection of a MappingFrame material. Reads surface_data, so
// caster_world comes first.
struct CasterLocal {
    position: vec3f,
    normal: vec3f,
};

fn caster_local(v: VertexIn) -> CasterLocal {
    var position = v.position;
    var normal = v.normal;
    var derive = false;
    if (surface_data.p[19].x != 0.0) {
        position = v.position;
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
    return CasterLocal(position, normal);
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

// The alpha test attributes of a vertex, with the UV transformed as vs of mesh.wgsl does. Reads
// surface_data, so the position comes first.
fn cutout_attributes(v: VertexIn, o: ptr<function, CasterOut>) {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material & 0xFFFFFFu);
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
        cutout_mapping(v, material, v.position, v.normal, o);
    } else if (material_frame(material)) {
        let local = caster_local(v);
        cutout_mapping(v, material, local.position, local.normal, o);
    }
    (*o).alpha = shading.alpha;
    (*o).cutoff = material.cutoff;
}

// position and normal: the object position and normal of the vertex.
fn cutout_mapping(v: VertexIn, material: Material, position: vec3f, normal: vec3f,
    o: ptr<function, CasterOut>) {
    let model = instance_model(transforms[v.slot]);
    let c0 = model[0].xyz;
    let c1 = model[1].xyz;
    let c2 = model[2].xyz;
    let scale = vec3f(length(c0), length(c1), length(c2));
    (*o).mapped = 1.0 + material_blend(material);
    (*o).uv = mapping_offset(material);
    (*o).object = mapping_object(material, position, scale);
    (*o).object_normal = normal;
    if (material_world(material)) {
        let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
        (*o).object = mapping_object(material, (model * vec4f(position, 1.0)).xyz,
            vec3f(1.0));
        (*o).object_normal = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1)) *
            normal * flip;
    } else if (material_frame(material)) {
        var relative = model;
        if (v.slot < arrayLength(&frames)) {
            relative = instance_frame(frames[v.slot], model);
        }
        let f = mapping_frame(material, relative, position, normal);
        (*o).object = f.object;
        (*o).object_normal = f.normal;
    }
}

fn cutout_attributes_mesh(v: VertexIn, o: ptr<function, CasterOut>) {
    let shading = shadings[v.slot];
    let material = material_fetch(shading.material & 0xFFFFFFu);
    (*o).uv = v.uv * material.uv.xy + material.uv.zw;
    (*o).mapped = 0.0;
    (*o).object = vec4f(0.0);
    (*o).object_normal = vec3f(0.0);
    if (material_mapped(material)) {
        cutout_mapping(v, material, v.position, v.normal, o);
    }
    (*o).alpha = shading.alpha;
    (*o).cutoff = material.cutoff;
}

@vertex fn vs(v: VertexIn) -> ShadowOut {
    return ShadowOut(cascade.matrix * vec4f(caster_world_mesh(v.slot, v.position), 1.0));
}

@vertex fn vs_surface(v: VertexIn) -> ShadowOut {
    return ShadowOut(caster_position(v.slot, v.position));
}

@vertex fn vs_cutout(v: VertexIn) -> CasterOut {
    var o: CasterOut;
    o.pos = cascade.matrix * vec4f(caster_world_mesh(v.slot, v.position), 1.0);
    cutout_attributes_mesh(v, &o);
    return o;
}

@vertex fn vs_cutout_surface(v: VertexIn) -> CasterOut {
    var o: CasterOut;
    o.pos = caster_position(v.slot, v.position);
    cutout_attributes(v, &o);
    return o;
}

// Discard where the alpha times the albedo alpha of the batch is below the cutoff: at the
// interpolated UV, or projected for a projected mapping (mapped not 0),
// blended over the three axes with a blend (mapped above 1), as mapping_alpha makes it.
fn cutout_test(uv: vec2f, alpha: f32, cutoff: f32, mapped: f32, object: vec4f,
    object_normal: vec3f) {
    if (alpha * mapping_alpha(albedo_map, material_sampler, uv, mapped, object, object_normal) <
        cutoff) {
        discard;
    }
}

@fragment fn fs() {
}

@fragment fn fs_cutout(f: CasterOut) {
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

@fragment fn fs_cutout_blend(f: CasterOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
}

#ifdef BOX_SHADOW_CLIP_DISTANCES
fn octant_clip(world: vec3f) -> array<f32, 3> {
    let d = octant_distances(world);
    return array<f32, 3>(d.x, d.y, d.z);
}
#else
fn octant_clip(world: vec3f) -> vec3f {
    return octant_distances(world);
}
#endif

fn point_out(world: vec3f, c: CasterOut) -> PointOut {
    var o: PointOut;
    o.pos = octant_position(world);
    o.uv = c.uv;
    o.alpha = c.alpha;
    o.cutoff = c.cutoff;
    o.mapped = c.mapped;
    o.object = c.object;
    o.object_normal = c.object_normal;
    o.clip = octant_clip(world);
    return o;
}

@vertex fn vs_point(v: VertexIn) -> PointShadowOut {
    let world = caster_world_mesh(v.slot, v.position);
    return PointShadowOut(octant_position(world), octant_clip(world));
}

@vertex fn vs_point_surface(v: VertexIn) -> PointShadowOut {
    let world = caster_world(v.slot, v.position);
    return PointShadowOut(octant_position(world), octant_clip(world));
}

@vertex fn vs_point_cutout(v: VertexIn) -> PointOut {
    let world = caster_world_mesh(v.slot, v.position);
    var c: CasterOut;
    cutout_attributes_mesh(v, &c);
    return point_out(world, c);
}

@vertex fn vs_point_cutout_surface(v: VertexIn) -> PointOut {
    let world = caster_world(v.slot, v.position);
    var c: CasterOut;
    cutout_attributes(v, &c);
    return point_out(world, c);
}

#ifdef BOX_SHADOW_CLIP_DISTANCES
// The clip distances already kept the fragment to its octant.
@fragment fn fs_point_cutout(f: CasterOut) {
    cutout_test(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
}

@fragment fn fs_point_cutout_blend(f: CasterOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
}
#else
// Discard a fragment outside the octant of its draw, which core WebGPU cannot clip.
fn octant_discard(clip: vec3f) {
    if (any(clip < vec3f(0.0))) {
        discard;
    }
}

@fragment fn fs_point(f: PointShadowOut) {
    octant_discard(f.clip);
}

@fragment fn fs_point_cutout(f: PointOut) {
    cutout_test(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
    octant_discard(f.clip);
}

@fragment fn fs_point_cutout_blend(f: PointOut) {
    cutout_test_blend(f.uv, f.alpha, f.cutoff, f.mapped, f.object, f.object_normal);
    octant_discard(f.clip);
}
#endif
