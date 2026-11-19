// The material table (box_render_material_t): one record per prefab or mesh shape that owns its
// material, slot 0 the default material, which a mesh instance references by the material id of
// its shading (MeshShading of instance.wgsl). The tint and the alpha stay in the instance.
//
// A shader that includes this file declares the table as `materials`, binding 4 of the group of
// the instance streams:
//
//   @group(2) @binding(4) var<storage, read> materials: array<Material>;
#pragma once

struct Material {
    // Display encoded base color (Rgb), r | g << 8 | b << 16
    color: u32,
    // Metallic and roughness, 0..1
    metallic: f32,
    roughness: f32,
    // AlphaCutoff of an alpha tested material, 0 without one
    cutoff: f32,
    // Linear emitted color times its strength
    emissive: vec3f,
    // Display encoded Transmission.attenuation_color, r | g << 8 | b << 16, white without one
    attenuation_color: u32,
    // TextureTransform: u scale, v scale, u offset, v offset, the offset plus the wrapped
    // TextureScroll.velocity offset
    uv: vec4f,
    // Transmission: factor (0 without one), index of refraction, thickness and the distance at
    // which light has the attenuation color
    transmission_factor: f32,
    ior: f32,
    thickness_factor: f32,
    attenuation_distance: f32,
    // ClearCoat strength | roughness << 16 as two unorm16 (unpack2x16unorm), 0 without one
    clear_coat: u32,
    // SpecularAnisotropy strength | rotation / 2 pi << 16 as two unorm16 (unpack2x16unorm), 0
    // without one
    anisotropy: u32,
    // Sheen r | g << 8 | b << 16 | roughness << 24 as unorm8 (unpack4x8unorm), the color display
    // encoded, 0 without one
    sheen: u32,
    // DiffuseTransmission r | g << 8 | b << 16 | factor << 24 as unorm8 (unpack4x8unorm), the
    // color display encoded, 0 without one
    diffuse_transmission: u32,
    // DiffuseTransmission.thickness, world units behind the surface of its shadow lookup
    thickness: f32,
    // DiffuseTransmission.blur, world units of blur radius per world unit behind the surface of
    // a Transmission shape, 0 without one
    diffuse_blur: f32,
    // TextureMapping.mode | blend << 16, the blend a unorm16 (unpack2x16unorm): mode 0 the UVs
    // of the mesh, 1 projected from the object position, 2 from the world position
    mapping: u32,
    // TextureMapping.size of a MappingObject or MappingWorld material, world units per repeat;
    // 0 for the UVs
    mapping_size: f32,
    // Wrapped TextureScroll.normal_velocity offset the normal map adds to its UVs, 0 without one
    normal_offset: vec2f,
    // Fresnel.color in linear light as unorm16 (unpack2x16unorm): r | g << 16, b | 0 << 16
    // A Water material, which ignores Fresnel, holds its WaterCaustics in the six Fresnel words
    // instead, all 0 without one: the offset of its water frame in cells (wrapped to
    // CAUSTICS_PERIOD) as two f32, its strength (0 for none), 1 over its scale, its depth and
    // the phase of its animation (0..1 over CAUSTICS_LOOP cells)
    fresnel_color: vec2u,
    // Fresnel strength, power, bias and opacity (0..1), all 0 without one
    fresnel_strength: f32,
    fresnel_power: f32,
    fresnel_bias: f32,
    fresnel_opacity: f32,
    // Wrapped Water.ripple_velocity offset of the second normal sample, 0 without one
    water_ripple_offset: vec2f,
    // Water shallow, deep and foam colors, display encoded, r | g << 8 | b << 16, 0 without one
    water_shallow: u32,
    water_deep: u32,
    water_foam: u32,
    // Water depth and visibility, at least WATER_MIN; 0 without one
    water_depth: f32,
    water_visibility: f32,
    // Water refraction, a fraction of the image height
    water_refraction: f32,
    // Water depth below which foam appears, 0 for none
    water_foam_width: f32,
    // Repeats of the foam texture per repeat of the UVs
    water_foam_scale: f32,
    // Scale of the UVs of the second normal sample, 0 for none
    water_ripple_scale: f32,
    // WATER_FOAM_TEXTURE when the foam samples its texture in bit 0, the set of its WaterWaves in
    // the wave table in bits 1..7 (WATER_WAVES_SHIFT, 0 for none); above them Water.absorption
    // display encoded, r << 8 | g << 16 | b << 24 (water_absorption)
    water_flags: u32,
    // Distortion strength (a fraction of the image height), 1 over its scale and the phase of
    // its noise (speed times the time, wrapped to 0..1), all 0 without one
    distortion_strength: f32,
    distortion_frequency: f32,
    distortion_phase: f32,
    // DISTORTION_PRESENT with a Distortion, DISTORTION_SHARP too without Transmission; 0 without
    // one
    // A Water material, which ignores Distortion, holds the flow mapping of its textures in the
    // four Distortion words instead, all 0 without WaterFlow.textures: its WaterFlow velocity
    // (world X and Z), the phase of the flow mapping (0..1 over WATER_FLOW_PERIOD seconds) and
    // WATER_FLOW_TEXTURES (vo_water_flow)
    distortion_flags: u32,
    // The Wind of a WindSway material (Scene.wind), all 0 without one: its normalized world X and
    // Z direction, the wrapped world offset of its gust field, its speed, gust (0..1), 1 over its
    // gust scale and the phase of its sway (0..1)
    wind_direction: vec2f,
    wind_offset: vec2f,
    wind_speed: f32,
    wind_gust: f32,
    wind_frequency: f32,
    wind_sway: f32,
    // WindSway bend, height, base, flutter and flutter reach, and the phase of its flutter (0..1)
    wind_bend: f32,
    wind_height: f32,
    wind_base: f32,
    wind_flutter: f32,
    wind_reach: f32,
    wind_flutter_phase: f32,
    // WIND_PRESENT with a WindSway; REVEAL_PRESENT with a VertexReveal, REVEAL_INVERT, its data
    // channel 0..7 at REVEAL_CHANNEL_SHIFT and its softness (0 for a hard edge) as a half float
    // in the high 16 bits (unpack2x16float); 0 without either
    flags: u32,
    // VertexReveal threshold, 0 without one
    reveal_threshold: f32,
};

// Smallest depth and visibility of a Water record (BOX_RENDER_WATER_MIN).
const WATER_MIN: f32 = 1e-3;

// Flag of a Water record whose foam samples its texture (BOX_RENDER_WATER_FOAM_TEXTURE).
const WATER_FOAM_TEXTURE: u32 = 1u;

// Flag of the flow words of a Water record whose normal map and foam texture move with the
// flow (BOX_RENDER_WATER_FLOW_TEXTURES).
const WATER_FLOW_TEXTURES: u32 = 1u;

// Seconds of a cycle of the two phase flow mapping (BOX_RENDER_WATER_FLOW_PERIOD).
const WATER_FLOW_PERIOD: f32 = 1.0;

// Bit of the flags of a Water record at which the set of its waves starts, and its mask
// (BOX_RENDER_WATER_WAVES_SHIFT, BOX_RENDER_WATER_WAVES_MASK).
const WATER_WAVES_SHIFT: u32 = 1u;
const WATER_WAVES_MASK: u32 = 0x7Fu;

// Flag of a record with a Distortion (BOX_RENDER_DISTORTION_PRESENT).
const DISTORTION_PRESENT: u32 = 1u;

// Flag of a Distortion record without Transmission (BOX_RENDER_DISTORTION_SHARP): its
// transmission is a factor of 1 without refraction, Fresnel, tint or absorption, and it samples
// the sharp snapshot.
const DISTORTION_SHARP: u32 = 2u;

// Below this weight a projection of a triplanar mapping is dropped and the others renormalized,
// so a fragment whose normal is (nearly) along an axis samples once.
const TRIPLANAR_MIN_WEIGHT: f32 = 0.01;

// Flags of the VertexReveal of a record (BOX_RENDER_REVEAL_PRESENT, BOX_RENDER_REVEAL_INVERT)
// and the bit its data channel starts at (BOX_RENDER_REVEAL_CHANNEL_SHIFT).
const REVEAL_PRESENT: u32 = 2u;
const REVEAL_INVERT: u32 = 4u;
const REVEAL_CHANNEL_SHIFT: u32 = 3u;

// Reveal edge of a vertex without a VertexReveal or without data: always drawn.
const REVEAL_NONE: f32 = 1e4;

// Smallest softness a reveal edge divides by, which makes a hard edge of a softness of 0.
const REVEAL_HARD: f32 = 1e-5;

// Reveal edge of a vertex of a VertexReveal material from its per vertex data: the distance of
// its channel above the threshold (below it with REVEAL_INVERT) over the softness, which is
// linear in the channel, so the interpolated edge is that of the interpolated channel. The
// fragments with an edge below 0 are hidden, those above 1 drawn, those between dithered
// (revealed). REVEAL_NONE for a material without one.
fn reveal_edge(m: Material, data0: vec4f, data1: vec4f) -> f32 {
    var result = REVEAL_NONE;
    if ((m.flags & REVEAL_PRESENT) != 0u) {
        let channel = (m.flags >> REVEAL_CHANNEL_SHIFT) & 7u;
        let value = select(data0[channel & 3u], data1[channel & 3u], channel >= 4u);
        var d = value - m.reveal_threshold;
        if ((m.flags & REVEAL_INVERT) != 0u) {
            d = -d;
        }
        result = d / max(unpack2x16float(m.flags).y, REVEAL_HARD);
    }
    return result;
}

// Whether a fragment with an interpolated reveal edge is drawn at a pixel: kept where the edge
// is above an interleaved gradient noise of the pixel in 0..1, so a soft edge is dithered and a
// hard one cut at the threshold.
fn revealed(edge: f32, pixel: vec2f) -> bool {
    let p = floor(pixel);
    let dither = fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
    return edge >= 0.0 && edge >= dither;
}

// Record of a material id; an id past the table reads the default material of slot 0.
fn material_fetch(id: u32) -> Material {
    return materials[select(0u, id, id < arrayLength(&materials))];
}

// The material projects its texture coordinates from the object or the world position
// (MappingObject or MappingWorld).
fn material_mapped(m: Material) -> bool {
    return (m.mapping & 0xffffu) != 0u;
}

// The material projects its texture coordinates from the world position (MappingWorld).
fn material_world(m: Material) -> bool {
    return (m.mapping & 0xffffu) == 2u;
}

// The material projects its texture coordinates from the position in the space of the texture
// frame of its instance (MappingFrame).
fn material_frame(m: Material) -> bool {
    return (m.mapping & 0xffffu) == 3u;
}

// TextureMapping.blend of the material, 0..1, 0 for the hard projection.
fn material_blend(m: Material) -> f32 {
    return unpack2x16unorm(m.mapping).y;
}

// What a vertex of a MappingObject material passes on for the projection of its fragments: its
// object position times the scale of its instance (the lengths of the model matrix columns, over
// the uniform world scale of its texture frame if any) over the mapping size, with the
// TextureTransform scale of the texture axis each one maps to: (x * su, y * sv, z * su, x * sv).
// It is linear in the position, so it interpolates exactly.
// A MappingWorld material passes its world position with a scale of 1.
fn mapping_object(m: Material, position: vec3f, scale: vec3f) -> vec4f {
    let p = position * scale / m.mapping_size;
    return vec4f(p.x * m.uv.x, p.y * m.uv.y, p.z * m.uv.x, p.x * m.uv.y);
}

// What a vertex of a MappingFrame material passes on: its object position moved by relative,
// the transform of its instance relative to its texture frame, over the mapping size as
// mapping_object makes it (with a scale of 1, so the local units of the frame), and its object
// normal in the space of the frame. Both are linear in the position, so they interpolate exactly.
struct FrameMapping {
    object: vec4f,
    normal: vec3f,
};

fn mapping_frame(m: Material, relative: mat4x4f, position: vec3f, normal: vec3f)
    -> FrameMapping
{
    let c0 = relative[0].xyz;
    let c1 = relative[1].xyz;
    let c2 = relative[2].xyz;
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let n = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1)) * normal * flip;
    return FrameMapping(mapping_object(m, (relative * vec4f(position, 1.0)).xyz, vec3f(1.0)), n);
}

// Rotation of the texture frame of an instance in the world as a quaternion, which turns the
// axes of a MappingFrame projection to the world: the model matrix times the inverse of the
// transform relative to the frame (the adjugate, by the sign of the determinant), whose columns
// mapping_rotation normalizes.
fn mapping_frame_rotation(model: mat4x4f, relative: mat4x4f) -> vec4f {
    let c0 = relative[0].xyz;
    let c1 = relative[1].xyz;
    let c2 = relative[2].xyz;
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let adjugate = transpose(mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1)));
    let w = mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz) * adjugate * flip;
    return mapping_rotation(w[0], w[1], w[2]);
}

// Rotation of normalized model matrix columns as a quaternion (xyz the axis times the sine of
// half the angle, w its cosine), which turns the axes of a MappingObject projection to the
// world. A mirrored model (negative determinant) turns as its first column negated.
fn mapping_rotation(c0: vec3f, c1: vec3f, c2: vec3f) -> vec4f {
    let x = normalize(c0) * select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let y = normalize(c1);
    let z = normalize(c2);
    let trace = x.x + y.y + z.z;
    var q: vec4f;
    if (trace > 0.0) {
        let r = sqrt(1.0 + trace) * 2.0;
        q = vec4f((y.z - z.y) / r, (z.x - x.z) / r, (x.y - y.x) / r, 0.25 * r);
    } else if (x.x > y.y && x.x > z.z) {
        let r = sqrt(max(1.0 + x.x - y.y - z.z, 1e-12)) * 2.0;
        q = vec4f(0.25 * r, (y.x + x.y) / r, (z.x + x.z) / r, (y.z - z.y) / r);
    } else if (y.y > z.z) {
        let r = sqrt(max(1.0 + y.y - x.x - z.z, 1e-12)) * 2.0;
        q = vec4f((y.x + x.y) / r, 0.25 * r, (z.y + y.z) / r, (z.x - x.z) / r);
    } else {
        let r = sqrt(max(1.0 + z.z - x.x - y.y, 1e-12)) * 2.0;
        q = vec4f((z.x + x.z) / r, (z.y + y.z) / r, 0.25 * r, (x.y - y.x) / r);
    }
    return normalize(q);
}

fn mapping_rotate(q: vec4f, v: vec3f) -> vec3f {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// What mapping_uv adds after the projection: the TextureTransform offset, and half the scale,
// so a unit cube with a size of 1 maps as its UVs do.
fn mapping_offset(m: Material) -> vec2f {
    return 0.5 * m.uv.xy + m.uv.zw;
}

// Texture coordinates of a Sweep vertex as lengths in the units of its path, for a MappingObject
// material: on the walls the uv fractions times the lengths in p[9] (x outer ring, y holes, whose
// u starts at 2, z along the path), on the caps uv times p[2].xy. The region is the z of a grid
// vertex (0 and 1 the walls, 8 and 9 the caps), or on a baked render mesh (p[19]) the per vertex
// region in surface_assets (0 and 1 the walls, 3 the caps). See box_surface_build_lengths of
// src/geometry/surface_math.c.
fn surface_sweep_lengths(uv: vec2f, position: vec3f, vertex: u32) -> vec2f {
    let p = surface_data.p;
    var region = i32(position.z);
    if (p[19].x != 0.0) {
        region = i32(surface_assets[vertex / 4u][vertex % 4u]);
    }
    var result = uv * p[2].xy;
    if (region == 0) {
        result = uv * p[9].xz;
    } else if (region == 1) {
        result = vec2f((uv.x - 2.0) * p[9].y, uv.y * p[9].z);
    }
    return result;
}

// Texture coordinates of a fragment of a MappingObject, MappingWorld or MappingFrame material,
// projected onto the plane of the dominant axis of its object (world, frame) normal, oriented
// as the faces of a Box: on the side faces u runs along the horizontal tangent seen from
// outside (up x normal) and v toward -y; the top has u along +z and the bottom along -z, both v
// along -x. object comes from mapping_object, offset from mapping_offset. It does not branch, so a texture sample after
// it stays in uniform control flow.
fn mapping_uv(object: vec4f, normal: vec3f, offset: vec2f) -> vec2f {
    let a = abs(normal);
    let s = select(vec3f(-1.0), vec3f(1.0), normal >= vec3f(0.0));
    let side_x = vec2f(-s.x * object.z, -object.y);
    let side_z = vec2f(s.z * object.x, -object.y);
    let top = vec2f(s.y * object.z, -object.w);
    let side = select(side_z, side_x, a.x >= a.z);
    return select(side, top, a.y >= max(a.x, a.z)) + offset;
}

// The axis mapping_uv projects on: 0 for x, 1 for y, 2 for z.
fn mapping_axis(normal: vec3f) -> u32 {
    let a = abs(normal);
    return select(select(2u, 0u, a.x >= a.z), 1u, a.y >= max(a.x, a.z));
}

// The three axis projections of a triplanar mapping, as mapping_uv orients each one: the
// columns hold the texture coordinates of the x, y and z projections of object (or of its
// screen derivatives) without the offset, s the signs of the normal.
fn mapping_planes(object: vec4f, s: vec3f) -> mat3x2f {
    return mat3x2f(vec2f(-s.x * object.z, -object.y), vec2f(s.y * object.z, -object.w),
        vec2f(s.z * object.x, -object.y));
}

// Signs of the components of a normal, +1 for 0, as mapping_uv takes them.
fn mapping_signs(normal: vec3f) -> vec3f {
    return select(vec3f(-1.0), vec3f(1.0), normal >= vec3f(0.0));
}

// Weights of the x, y and z projections of a triplanar mapping with a blend above 0: the
// normal relative to its largest component raised to 1 / blend, normalized; those below
// TRIPLANAR_MIN_WEIGHT are 0 and the rest renormalized. The axis of mapping_uv always keeps a
// weight of at least a third.
fn mapping_weights(normal: vec3f, blend: f32) -> vec3f {
    let a = abs(normal);
    let r = a / max(max(a.x, max(a.y, a.z)), 1e-20);
    let w = pow(max(r, vec3f(1e-6)), vec3f(1.0 / max(blend, 1e-4)));
    let t = w / (w.x + w.y + w.z);
    let kept = select(t, vec3f(0.0), t < vec3f(TRIPLANAR_MIN_WEIGHT));
    return kept / (kept.x + kept.y + kept.z);
}

// Tangent (along u) and bitangent (up the image, toward smaller v) of a projection of
// mapping_planes in the frame of its axes, the projection axis times its sign completing them
// to a right handed frame: x (0, 0, -s.x), (0, 1, 0); y (0, 0, s.y), (1, 0, 0); z (s.z, 0, 0),
// (0, 1, 0).
fn mapping_tangent(axis: u32, s: vec3f) -> vec3f {
    return select(select(vec3f(s.z, 0.0, 0.0), vec3f(0.0, 0.0, s.y), axis == 1u),
        vec3f(0.0, 0.0, -s.x), axis == 0u);
}

fn mapping_bitangent(axis: u32) -> vec3f {
    return select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), axis == 1u);
}

// Albedo alpha of an alpha tested fragment. mapped is 0 for mesh UVs, nonzero for a projected
// mapping (uv its offset), above 1 for a triplanar blend. mapped is flat, the same for every
// fragment of a primitive and so of a quad, so the derivatives of either branch hold.
@diagnostic(off, derivative_uniformity)
fn mapping_alpha(albedo: texture_2d<f32>, smp: sampler, uv: vec2f, mapped: f32, object: vec4f,
    normal: vec3f) -> f32 {
    var alpha = 0.0;
    if (mapped > 1.0) {
        let dx = dpdx(object);
        let dy = dpdy(object);
        let w = mapping_weights(normal, mapped - 1.0);
        let s = mapping_signs(normal);
        var planes = mapping_planes(object, s);
        var gx = mapping_planes(dx, s);
        var gy = mapping_planes(dy, s);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] > 0.0) {
                alpha += w[k] * textureSampleGrad(albedo, smp, planes[k] + uv, gx[k], gy[k]).a;
            }
        }
    } else {
        var at = uv;
        if (mapped != 0.0) {
            at = mapping_uv(object, normal, uv);
        }
        alpha = textureSample(albedo, smp, at).a;
    }
    return alpha;
}

// Rim factor of a Fresnel: saturate(bias + pow(1 - saturate(ndv), power)), ndv the cosine
// between the shading normal and the direction toward the camera. box_draws_fresnel_factor is
// its C twin.
fn fresnel_factor(ndv: f32, power: f32, bias: f32) -> f32 {
    let x = max(1.0 - saturate(ndv), 1e-6);
    return saturate(bias + pow(x, power));
}

// Mix factor of the deep color of a Water body at a water depth d along its normal:
// 1 - exp(-3 d / depth), 1 where nothing is behind (deep). box_draws_water_depth_mix is its C
// twin.
fn water_depth_mix(d: f32, depth: f32, deep: bool) -> f32 {
    if (deep) {
        return 1.0;
    }
    return saturate(1.0 - exp(-3.0 * max(d, 0.0) / max(depth, WATER_MIN)));
}

// Share of the scene behind a Water that shows through ray_length of its body:
// exp(-3 ray_length / visibility), 0 where nothing is behind (deep).
// box_draws_water_transmittance is its C twin.
fn water_transmittance(ray_length: f32, visibility: f32, deep: bool) -> f32 {
    if (deep) {
        return 0.0;
    }
    return saturate(exp(-3.0 * max(ray_length, 0.0) / max(visibility, WATER_MIN)));
}

// Water.absorption of a record in linear light, from the bytes above the flags of water_flags;
// exactly 1 for a channel of 255, so a white absorption leaves the scene behind as it is.
fn water_absorption(m: Material) -> vec3f {
    let c = unpack4x8unorm(m.water_flags).yzw;
    return select(material_linear(c), vec3f(1.0), c >= vec3f(1.0));
}

// Tint of the scene behind a Water that shows through ray_length of its body, per channel:
// absorption (linear) to the power of ray_length / visibility; 1 where nothing is behind (deep)
// and for a white absorption. box_draws_water_absorption is its C twin.
fn water_absorbed(absorption: vec3f, ray_length: f32, visibility: f32, deep: bool) -> vec3f {
    if (deep) {
        return vec3f(1.0);
    }
    let e = max(ray_length, 0.0) / max(visibility, WATER_MIN);
    return saturate(exp2(e * log2(clamp(absorption, vec3f(1e-6), vec3f(1.0)))));
}

// Foam of a Water at a water depth d: saturate(1 - d / foam_width), 0 for a width of 0 or where
// nothing is behind; a texture cuts the band at its red, smoothstep(t, t + 0.1, tex) with
// t = 1 - band. box_draws_water_foam is its C twin.
fn water_foam(d: f32, foam_width: f32, deep: bool, textured: bool, tex: f32) -> f32 {
    var band = 0.0;
    if (foam_width > 0.0 && !deep) {
        band = saturate(1.0 - max(d, 0.0) / foam_width);
    }
    if (textured) {
        let t = 1.0 - band;
        band = select(0.0, smoothstep(t, t + 0.1, tex), band > 0.0);
    }
    return band;
}

// Display encoded color to linear light.
fn material_linear(c: vec3f) -> vec3f {
    return select(pow((c + vec3f(0.055)) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}

// Base color of a material mixed toward a composed tint (xyz added, w kept, see
// mesh_shading_tint) in display encoding, then converted to linear light.
fn material_color(m: Material, tint: vec4f) -> vec3f {
    let base = unpack4x8unorm(m.color).rgb;
    return material_linear(clamp(base * tint.w + tint.xyz, vec3f(0.0), vec3f(1.0)));
}
