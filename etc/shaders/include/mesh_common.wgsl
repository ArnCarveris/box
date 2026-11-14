// Bindings, vertex stage and depth only entry points shared by the mesh shaders: mesh.wgsl draws
// the meshes without the material features of mesh_extended.wgsl, which draws the rest.
// Both share the pipeline layouts of the forward pass and the scene features below, set per
// pipeline from the Scene of the scene it draws (box_gpu_feature_constants): a scene
// with a feature off compiles its pipelines without that code.
#pragma once

#include "instance.wgsl"
#include "material.wgsl"
#include "lights.wgsl"
#include "attenuation.wgsl"
#include "shadow.wgsl"
#include "spot_shadow.wgsl"
#include "point_shadow.wgsl"
#include "cookie.wgsl"
#include "ambient.wgsl"
#include "probes.wgsl"
#include "aerial.wgsl"
#include "planet_light.wgsl"

// Probe volume irradiance with the visibility of the probes (Scene.baked).
override scene_baked: bool = false;

// Reflection probes, normalized by the probe volume with scene_baked
// (Scene.reflections).
override scene_reflections: bool = false;

// Shadows of the spot and point lights with a tile (Scene.punctual_shadows).
override scene_punctual_shadows: bool = false;

// Cookie of the first directional light (Scene.cookies).
override scene_cookies: bool = false;

// Prefiltered environment cubemap and its SH ambient (Scene.environment).
override scene_environment: bool = false;

// Aerial perspective of the atmosphere (Scene.aerial_perspective).
override scene_aerial: bool = false;

// Normal map offset of a TextureScroll (Scene.texture_scroll), its UV scroll is in the uv
// transform of the material record.
override material_texture_scroll: bool = false;

// The sun of the PlanetAtmosphere of an instance reaches its fragments through the
// transmittance of the atmosphere (Scene.planet_light): the vertex stage passes the planet light
// slot of the instance and the direct light attenuates the sun (planet_light.wgsl).
override scene_planet_light: bool = false;

// Fresnel of the extended mesh shader (Scene.fresnel): the vertex stage passes the Fresnel of the
// material and the fragment stage adds its rim. The mesh shader leaves it false.
override material_fresnel: bool = false;

// The shapes of PlanarReflections reflect the mirrored images of the view
// (Scene.planar_reflections): the vertex stage finds the reflection slot of an instance
// (planar_owner). The mesh shader leaves it false.
override scene_planar: bool = false;

// Water shapes of a scene with Scene.water, set by the extended mesh shader with transmissive:
// the vertex stage passes the Water fields of the material (vo_water).
override water: bool = false;

// Transmissive shapes of a scene with Scene.distortion, set by the extended mesh shader with
// transmissive: the vertex stage passes the Distortion of the material (vo_distortion) and the
// position its noise is sampled at, and the snapshot is offset by the noise. Water pipelines
// ignore it.
override material_distortion: bool = false;

const PI: f32 = 3.141592653589793;
const MIN_ROUGHNESS: f32 = 0.045;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> clusters: array<u32>;

@group(1) @binding(0) var albedo_map: texture_2d<f32>;
@group(1) @binding(1) var emissive_map: texture_2d<f32>;
@group(1) @binding(2) var orm_map: texture_2d<f32>;
@group(1) @binding(3) var normal_map: texture_2d<f32>;
@group(1) @binding(4) var material_sampler: sampler;

// Reflection probes of the scene in blend order (box_render_reflection_probe_t), the cubemap
// array with a slot per probe, mip k of N prefiltered for the perceptual roughness k / (N - 1)
// (bake_sh.wgsl), and the L2 SH irradiance of each slot, as a probe of the volume holds it.
@group(0) @binding(5) var<storage, read> reflection_probes: array<ReflectionProbe>;
@group(0) @binding(6) var reflection_cubes: texture_cube_array<f32>;
@group(0) @binding(7) var reflection_sampler: sampler;
@group(0) @binding(8) var<storage, read> reflection_sh: array<vec4f>;

// Environment of the view (LightSettings.environment): a cubemap whose mip k of N is
// prefiltered for the perceptual roughness k / (N - 1), as a reflection probe slot, and its L2
// SH irradiance (ambient.wgsl). Views without one bind the black reflection slot and a zero SH,
// and their view.environment.w is 0.
@group(0) @binding(12) var environment_cube: texture_cube<f32>;

// Ambient occlusion of the view (Gtao) at the render size, read at the pixel of a fragment; a
// view without Gtao, and the probe and reflection captures, bind a 1x1 white image.
@group(0) @binding(14) var gtao_image: texture_2d<f32>;

@group(2) @binding(0) var<storage, read> transforms: array<InstanceTransform>;
@group(2) @binding(1) var<storage, read> surface_assets: array<vec4f>;
@group(2) @binding(2) var<storage, read> shadings: array<MeshShading>;
@group(2) @binding(3) var<storage, read> surfaces: array<SurfaceData>;
@group(2) @binding(4) var<storage, read> materials: array<Material>;
// Transforms relative to the texture frame, read by MappingFrame materials only: the frame
// pages have the stream, the other pages bind one zero record.
@group(2) @binding(5) var<storage, read> frames: array<InstanceTransform>;

// Texels per edge of the octahedral distance map of a probe.
const VISIBILITY_SIZE: i32 = 8;

// Offset along the normal of the point whose visibility from a probe is tested, in spacings.
const VISIBILITY_BIAS: f32 = 0.25;

// Smallest variance of the distance in a direction, in square spacings.
const VISIBILITY_VARIANCE: f32 = 0.001;

// Share of the weight of the probes around a point that the ambient makes up for when the
// probes the point sees carry less.
const VISIBILITY_FALLBACK: f32 = 0.1;

// Weight left over by the reflection probes of a fragment below which the next ones are
// skipped.
const REFLECTION_EPSILON: f32 = 1e-3;

// Range of the scale the irradiance grid normalizes the radiance of a reflection probe by: down
// to black for a fragment the grid sees no light at, up to twice as bright for one that is lit
// more than the capture point of the probe.
const REFLECTION_NORMALIZE_MIN: f32 = 0.0;
const REFLECTION_NORMALIZE_MAX: f32 = 2.0;

#include "place.wgsl"

struct VertexOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) @interpolate(flat) flat0: vec4u,
    @location(4) @interpolate(flat) flat1: vec4u,
    @location(5) uv: vec4f,
    @location(6) @interpolate(flat) flat2: vec4u,
    @location(7) @interpolate(flat) flat3: vec4u,
    @location(11) object: vec4f,
    @location(14) object_normal: vec3f,
    // Wrapped TextureScroll.normal_velocity offset of the material, added to the UVs of the
    // normal map only (normal_uv), 0 without material_texture_scroll
    @location(15) @interpolate(flat) normal_offset: vec2f,
    // Fresnel strength, power, bias and opacity of the material, 0 without one or without
    // material_fresnel
    @location(12) @interpolate(flat) fresnel: vec4f,
    // Fresnel color of the material in linear light as unorm16: r | g << 16, b
    @location(13) @interpolate(flat) fresnel_color: vec2u,
    // Water foam width, ripple and foam scale (pack2x16float) and ripple offset of a Water
    // pipeline (water), which reuses flat2 and flat3.zw for the other Water fields: Water
    // ignores Transmission, and the fragment stage cannot read the material table. Of the other
    // material_distortion pipelines, the Distortion of the material (vo_distortion).
    @location(8) @interpolate(flat) flat4: vec4u,
    // Low byte: planar reflection slot of the view plus one whose image the instance shows, 0
    // for none or without scene_planar (planar_owner). Above it: the planet light slot of the
    // instance, 0 for none or without scene_planet_light; one varying, as the fragment stage
    // has no inter stage variable left.
    @location(9) @interpolate(flat) planar: u32,
};

// Slot of the material table of an instance, below the planet light slot of its shading.
fn shading_material(s: MeshShading) -> u32 {
    return s.material & 0xFFFFFFu;
}

// Planet light slot of an instance (box_render_mesh_shading_t), 0 for none.
fn shading_planet(s: MeshShading) -> u32 {
    return s.material >> 24u;
}

// Make the sun of the planet of a fragment light it, with scene_planet_light.
fn planet_fragment(f: VertexOut) {
    if (scene_planet_light) {
        fragment_planet = f.planar >> 8u;
    }
}

// UVs of the normal map of a fragment: the UVs moved by the TextureScroll.normal_velocity offset
// with material_texture_scroll, else the UVs.
fn normal_uv(f: VertexOut, uv: vec2f) -> vec2f {
    if (material_texture_scroll) {
        return uv + f.normal_offset;
    }
    return uv;
}

// The Water fields of the material of a fragment of a Water pipeline, the others zero.
fn vo_water(f: VertexOut) -> Material {
    var m: Material;
    m.water_shallow = f.flat2.x & 0xFFFFFFu;
    m.water_flags = f.flat2.x >> 24u;
    m.water_deep = f.flat2.y;
    m.water_foam = f.flat2.z;
    m.water_depth = bitcast<f32>(f.flat2.w);
    m.water_visibility = bitcast<f32>(f.flat3.z);
    m.water_refraction = bitcast<f32>(f.flat3.w);
    m.water_foam_width = bitcast<f32>(f.flat4.x);
    m.water_ripple_scale = unpack2x16float(f.flat4.y).x;
    m.water_foam_scale = unpack2x16float(f.flat4.y).y;
    m.water_ripple_offset = bitcast<vec2f>(f.flat4.zw);
    return m;
}

// The Distortion of the material of a fragment of a material_distortion pipeline: strength,
// the factors that turn object.xyz into noise coordinates (x for its x and z, y for its y),
// phase and the DISTORTION_* flags, 0 without one.
struct DistortionOut {
    strength: f32,
    frequency: vec2f,
    phase: f32,
    flags: u32,
};

fn vo_distortion(f: VertexOut) -> DistortionOut {
    return DistortionOut(bitcast<f32>(f.flat4.x), unpack2x16float(f.flat4.y),
        bitcast<f32>(f.flat4.z), f.flat4.w);
}

fn vo_emissive(f: VertexOut) -> vec3f {
    return vec3f(unpack2x16float(f.flat0.x), unpack2x16float(f.flat0.y).x);
}

fn vo_model_scale(f: VertexOut) -> vec3f {
    return vec3f(unpack2x16float(f.flat0.y).y, unpack2x16float(f.flat0.z));
}

fn vo_sheen(f: VertexOut) -> vec4f {
    let sheen = unpack4x8unorm(f.flat0.w);
    return vec4f(material_linear(sheen.rgb), sheen.a);
}

fn vo_scalars(f: VertexOut) -> vec4f {
    return vec4f(unpack2x16float(f.flat1.x), unpack2x16float(f.flat1.y));
}

fn vo_layers(f: VertexOut) -> vec4f {
    return vec4f(unpack2x16unorm(f.flat1.z), unpack2x16unorm(f.flat1.w));
}

fn vo_diffuse_transmission(f: VertexOut) -> vec4f {
    let d = unpack4x8unorm(f.flat2.x);
    return vec4f(material_linear(d.rgb), d.a);
}

fn vo_attenuation(f: VertexOut) -> vec3f {
    return material_linear(unpack4x8unorm(f.flat2.y).rgb);
}

fn vo_transmission(f: VertexOut) -> vec4f {
    return bitcast<vec4f>(vec4u(f.flat2.zw, f.flat3.zw));
}

fn vo_rotation(f: VertexOut) -> vec4f {
    return vec4f(unpack2x16snorm(f.flat3.x), unpack2x16snorm(f.flat3.y));
}

// A MappingObject or MappingWorld material on a plain mesh, or a MappingFrame material on any
// mesh, sets mapped to 1 plus its blend, uv to the offset of its projection and object and
// object_normal to what mapping_uv projects in the fragment stage. A parametric surface keeps
// its own UVs otherwise.
struct VertexShading {
    color: vec4f,
    material: vec2f,
    uv: vec2f,
    mapped: f32,
    object: vec4f,
    object_normal: vec3f,
    rotation: vec4f,
};

fn vertex_base(v: VertexIn, shading: MeshShading, material: Material) -> VertexShading {
    var o: VertexShading;
    let tint = select(mesh_shading_tint(shading), vec4f(0.0, 0.0, 0.0, 1.0),
        view.reflection.y != 0u);
    o.color = vec4f(material_color(material, tint), shading.alpha);
    o.material = vec2f(material.metallic, material.roughness);
    o.uv = v.uv * material.uv.xy + material.uv.zw;
    o.mapped = 0.0;
    o.object = vec4f(0.0);
    o.object_normal = vec3f(0.0);
    o.rotation = vec4f(0.0, 0.0, 0.0, 1.0);
    return o;
}

// Transform of the instance of a slot relative to its texture frame, for a MappingFrame
// material; the model matrix for a slot past the frame stream of its page.
fn frame_model(slot: u32, model: mat4x4f) -> mat4x4f {
    var result = model;
    if (slot < arrayLength(&frames)) {
        result = instance_frame(frames[slot], model);
    }
    return result;
}

// Scale of the instance of a slot for a MappingObject material: the lengths of the columns of its
// frame record when it is under a texture frame (the model matrix over the uniform world scale
// of the frame), else scale, the lengths of the model matrix columns.
fn object_scale(slot: u32, model: mat4x4f, scale: vec3f) -> vec3f {
    var result = scale;
    if (slot < arrayLength(&frames)) {
        let relative = instance_frame(frames[slot], model);
        result = vec3f(length(relative[0].xyz), length(relative[1].xyz),
            length(relative[2].xyz));
    }
    return result;
}

fn vertex_mapping(o: ptr<function, VertexShading>, v: VertexIn, material: Material, p: Placed,
    scale: vec3f, normal: vec3f) {
    let blend = material_blend(material);
    (*o).mapped = 1.0 + blend;
    (*o).uv = mapping_offset(material);
    if (material_world(material)) {
        (*o).object = mapping_object(material, p.world.xyz, vec3f(1.0));
        (*o).object_normal = normal;
    } else if (material_frame(material)) {
        let relative = frame_model(v.slot, p.model);
        let f = mapping_frame(material, relative, p.local, p.normal);
        (*o).object = f.object;
        (*o).object_normal = f.normal;
        if (blend > 0.0) {
            (*o).rotation = mapping_frame_rotation(p.model, relative);
        }
    } else {
        (*o).object = mapping_object(material, v.position, object_scale(v.slot, p.model, scale));
        (*o).object_normal = v.normal;
        if (blend > 0.0) {
            (*o).rotation = mapping_rotation(p.model[0].xyz, p.model[1].xyz, p.model[2].xyz);
        }
    }
}

// p: the placed vertex; scale: the lengths of the model matrix columns of the instance; normal:
// the world normal of the vertex.
fn vertex_shading(v: VertexIn, shading: MeshShading, material: Material, p: Placed, scale: vec3f,
    normal: vec3f) -> VertexShading {
    var o = vertex_base(v, shading, material);
    if (material_mapped(material)) {
        vertex_mapping(&o, v, material, p, scale, normal);
    }
    return o;
}

fn vertex_shading_surface(v: VertexIn, shading: MeshShading, material: Material, p: Placed,
    scale: vec3f, normal: vec3f) -> VertexShading {
    var o = vertex_base(v, shading, material);
    if (material_mapped(material) &&
        (surface_data.p[0].x == 0.0 || material_frame(material))) {
        vertex_mapping(&o, v, material, p, scale, normal);
    }
    if (material_mapped(material) && !material_world(material) && !material_frame(material) &&
        surface_data.p[0].x == 1.0) {
        o.uv = surface_sweep_lengths(v.uv, v.position, v.vertex) / material.mapping_size *
            material.uv.xy + material.uv.zw;
    }
    if (surface_data.p[0].x != 0.0) {
        var region = i32(v.position.z);
        if (surface_data.p[0].x == 7.0) {
            // The caps are the only vertices with a vertical normal without a horizontal
            // part: the rims of a Bevel keep one up to the cap (box_profile_region)
            region = select(0, 1, v.uv.x >= 2.0);
            if (v.normal.x == 0.0 && v.normal.z == 0.0 && v.normal.y != 0.0) {
                region = 2;
            } else if (region == 1 && o.mapped == 0.0) {
                o.uv.x -= 2.0 * material.uv.x;
            }
        }
        var material_region = 2;
        if (region == 0 || region == 1) {
            material_region = region;
        } else if (region == 2 || region == 3 || region == 8 || region == 9) {
            material_region = 3;
        }
        if (surface_data.p[19].x != 0.0) {
            material_region = i32(surface_assets[v.vertex / 4u][v.vertex % 4u]);
        }
        let packed = bitcast<u32>(surface_data.p[21][material_region]);
        if ((packed & 0x80000000u) != 0u) {
            let tint = unpack4x8unorm(bitcast<u32>(surface_data.p[20][material_region]));
            let linear = select(pow((tint.rgb + vec3f(0.055)) / 1.055, vec3f(2.4)),
                tint.rgb / 12.92, tint.rgb <= vec3f(0.04045));
            o.color *= vec4f(linear, tint.a);
            o.material = vec2f(f32((packed >> 16u) & 32767u) / 32767.0,
                f32(packed & 65535u) / 65535.0);
        }
    }
    return o;
}

struct ModelFrame {
    normal: vec3f,
    scale: vec3f,
};

fn model_frame(p: Placed) -> ModelFrame {
    let c0 = p.model[0].xyz;
    let c1 = p.model[1].xyz;
    let c2 = p.model[2].xyz;
    let cofactor = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let scale = vec3f(length(c0), length(c1), length(c2));
    return ModelFrame(cofactor * p.normal * flip, scale);
}

fn vertex_scalars(material: Material, mapped: f32) -> vec2u {
    return vec2u(pack2x16float(vec2f(material.cutoff, material.thickness)),
        pack2x16float(vec2f(material.diffuse_blur, mapped)));
}

// The Distortion of a material_distortion pipeline: its noise follows the object position
// times the scale of the instance (world units that move and rotate with the shape) in object,
// which the fragment stage reads only for a projected mapping. A projected mapping keeps its
// object, the position in its projection space over the mapping size, times the uv scale on
// each axis (mapping_object): the frequency takes them out again.
fn vertex_distortion(o: ptr<function, VertexOut>, p: Placed, material: Material,
    frame: ModelFrame, mapped: f32) {
    var k = vec2f(material.distortion_frequency);
    if (mapped == 0.0) {
        (*o).object = vec4f(p.local * frame.scale, 0.0);
    } else {
        let uv = select(material.uv.xy, vec2f(1.0), material.uv.xy == vec2f(0.0));
        k = material.distortion_frequency * material.mapping_size / uv;
    }
    (*o).flat4 = vec4u(bitcast<u32>(material.distortion_strength), pack2x16float(k),
        bitcast<u32>(material.distortion_phase), material.distortion_flags);
}

fn vertex_out(p: Placed, material: Material, frame: ModelFrame, s: VertexShading)
    -> VertexOut
{
    var o: VertexOut;
    o.pos = p.pos;
    o.world = p.world.xyz;
    o.normal = frame.normal;
    o.color = s.color;
    o.flat0 = vec4u(pack2x16float(material.emissive.xy),
        pack2x16float(vec2f(material.emissive.z, frame.scale.x)),
        pack2x16float(frame.scale.yz), material.sheen);
    o.flat1 = vec4u(vertex_scalars(material, s.mapped), material.clear_coat,
        material.anisotropy);
    o.flat2 = vec4u(material.diffuse_transmission, material.attenuation_color,
        bitcast<u32>(material.transmission_factor), bitcast<u32>(material.ior));
    o.flat3 = vec4u(pack2x16snorm(s.rotation.xy), pack2x16snorm(s.rotation.zw),
        bitcast<u32>(material.thickness_factor), bitcast<u32>(material.attenuation_distance));
    o.object = s.object;
    o.object_normal = s.object_normal;
    o.uv = vec4f(s.uv, s.material);
    o.normal_offset = vec2f(0.0);
    if (material_texture_scroll) {
        o.normal_offset = material.normal_offset;
    }
    o.fresnel = vec4f(0.0);
    o.fresnel_color = vec2u(0u);
    if (material_fresnel) {
        o.fresnel = vec4f(material.fresnel_strength, material.fresnel_power,
            material.fresnel_bias, material.fresnel_opacity);
        o.fresnel_color = material.fresnel_color;
    }
    o.flat4 = vec4u(0u);
    if (water) {
        o.flat2 = vec4u((material.water_shallow & 0xFFFFFFu) | (material.water_flags << 24u),
            material.water_deep, material.water_foam, bitcast<u32>(material.water_depth));
        o.flat3.z = bitcast<u32>(material.water_visibility);
        o.flat3.w = bitcast<u32>(material.water_refraction);
        o.flat4 = vec4u(bitcast<u32>(material.water_foam_width),
            pack2x16float(vec2f(material.water_ripple_scale, material.water_foam_scale)),
            bitcast<vec2u>(material.water_ripple_offset));
    }
    if (material_distortion && !water && material.distortion_flags != 0u) {
        vertex_distortion(&o, p, material, frame, s.mapped);
    }
    return o;
}

struct DepthOut {
    @builtin(position) @invariant pos: vec4f,
};

struct CutoutOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) alpha: f32,
    @location(2) @interpolate(flat) scalars: vec2u,
    @location(3) object: vec4f,
    @location(4) object_normal: vec3f,
};

fn cutout_out(p: Placed, material: Material, s: VertexShading) -> CutoutOut {
    return CutoutOut(p.pos, s.uv, s.color.a, vertex_scalars(material, s.mapped), s.object,
        s.object_normal);
}

fn cutout_scalars(f: CutoutOut) -> vec4f {
    return vec4f(unpack2x16float(f.scalars.x), unpack2x16float(f.scalars.y));
}

// Planar reflection slot plus one of the view whose plane an instance is the shape of, 0 for
// none: the slot of the instance in its page and its transform match those the view records
// for the slot. The views without planar reflections, and the reflections themselves, record
// none.
fn planar_owner(slot: u32) -> u32 {
    let rows = transforms[slot].rows;
    var result = 0u;
    for (var k = 0u; k < 2u; k++) {
        if (view.planar_slots[k] == slot + 1u && all(rows[0] == view.planar_owner[k * 3u]) &&
            all(rows[1] == view.planar_owner[k * 3u + 1u]) &&
            all(rows[2] == view.planar_owner[k * 3u + 2u]))
        {
            result = k + 1u;
        }
    }
    return result;
}

@vertex fn vs(v: VertexIn) -> VertexOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading_material(shading));
    let p = place_mesh(v);
    let frame = model_frame(p);
    var o = vertex_out(p, material, frame,
        vertex_shading(v, shading, material, p, frame.scale, frame.normal));
    if (scene_planar) {
        o.planar = planar_owner(v.slot);
    }
    if (scene_planet_light) {
        o.planar |= shading_planet(shading) << 8u;
    }
    return o;
}

@vertex fn vs_surface(v: VertexIn) -> VertexOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading_material(shading));
    let p = place(v);
    let frame = model_frame(p);
    var o = vertex_out(p, material, frame,
        vertex_shading_surface(v, shading, material, p, frame.scale, frame.normal));
    if (scene_planar) {
        o.planar = planar_owner(v.slot);
    }
    if (scene_planet_light) {
        o.planar |= shading_planet(shading) << 8u;
    }
    return o;
}

@vertex fn vs_depth(v: VertexIn) -> DepthOut {
    return DepthOut(place_mesh(v).pos);
}

@vertex fn vs_depth_surface(v: VertexIn) -> DepthOut {
    return DepthOut(place(v).pos);
}

@vertex fn vs_cutout(v: VertexIn) -> CutoutOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading_material(shading));
    let p = place_mesh(v);
    let frame = model_frame(p);
    return cutout_out(p, material,
        vertex_shading(v, shading, material, p, frame.scale, frame.normal));
}

@vertex fn vs_cutout_surface(v: VertexIn) -> CutoutOut {
    let shading = shadings[v.slot];
    let material = material_fetch(shading_material(shading));
    let p = place(v);
    let frame = model_frame(p);
    return cutout_out(p, material,
        vertex_shading_surface(v, shading, material, p, frame.scale, frame.normal));
}

struct DrawKind {
    blend_mode: u32,
    transmissive: u32,
    alpha_cutout: u32,
    pad: u32,
};

@group(1) @binding(11) var<uniform> kind: DrawKind;

// Color of a fragment seen through the atmosphere between it and the camera, shaped for the
// blend equation: a fragment that covers the target is attenuated and adds the in scattered
// light weighted by its coverage, light added to or taken from the target is only attenuated,
// and a filter of the target is left as it is.
fn aerial_perspective(color: vec3f, alpha: f32, pos: vec4f, world: vec3f) -> vec3f {
    let ap = aerial_sample(pos, world);
    switch kind.blend_mode {
        case 0u, 1u: {
            return color * ap.a + ap.rgb;
        }
        case 2u: {
            return color * ap.a + ap.rgb * alpha;
        }
        case 4u, 7u: {
            return color;
        }
        default: {
            return color * ap.a;
        }
    }
}

fn output(color: vec3f, alpha: f32) -> vec4f {
    switch kind.blend_mode {
        case 0u: {
            return vec4f(color, 1.0);
        }
        case 4u, 7u: {
            return vec4f(mix(vec3f(1.0), color, alpha), alpha);
        }
        case 5u, 8u: {
            return vec4f(color * alpha, alpha);
        }
        default: {
            return vec4f(color, alpha);
        }
    }
}

// Depth prepass of a lit view (box_gpu_draw_prepass) for the meshes without an alpha test.
@fragment fn fs_depth() {
}

// Overdraw heat map of a view (box_gpu_debug_overdraw_record): the prepass again, each entry
// point adding one to the count of its kind where its fragment passes, the plain meshes in red
// and the alpha tested ones in green.
@fragment fn fs_count() -> @location(0) vec4f {
    return vec4f(1.0, 0.0, 0.0, 0.0);
}

@fragment fn fs_depth_cutout(f: CutoutOut) {
    let albedo = mapping_alpha(albedo_map, material_sampler, f.uv, cutout_scalars(f).w, f.object,
        f.object_normal);
    if (f.alpha * albedo < cutout_scalars(f).x) {
        discard;
    }
}

@fragment fn fs_count_cutout(f: CutoutOut) -> @location(0) vec4f {
    let albedo = mapping_alpha(albedo_map, material_sampler, f.uv, cutout_scalars(f).w, f.object,
        f.object_normal);
    if (f.alpha * albedo < cutout_scalars(f).x) {
        discard;
    }

    return vec4f(0.0, 1.0, 0.0, 0.0);
}
