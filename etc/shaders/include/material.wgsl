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
    // TextureTransform: u scale, v scale, u offset, v offset
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
};

// Below this weight a projection of a triplanar mapping is dropped and the others renormalized,
// so a fragment whose normal is (nearly) along an axis samples once.
const TRIPLANAR_MIN_WEIGHT: f32 = 0.01;

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

// TextureMapping.blend of the material, 0..1, 0 for the hard projection.
fn material_blend(m: Material) -> f32 {
    return unpack2x16unorm(m.mapping).y;
}

// What a vertex of a MappingObject material passes on for the projection of its fragments: its
// object position times the scale of its instance (the lengths of the model matrix columns) over
// the mapping size, with the TextureTransform scale of the texture axis each one maps to:
// (x * su, y * sv, z * su, x * sv). It is linear in the position, so it interpolates exactly.
// A MappingWorld material passes its world position with a scale of 1.
fn mapping_object(m: Material, position: vec3f, scale: vec3f) -> vec4f {
    let p = position * scale / m.mapping_size;
    return vec4f(p.x * m.uv.x, p.y * m.uv.y, p.z * m.uv.x, p.x * m.uv.y);
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

// A vector turned by a unit quaternion.
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
// material: on the walls (region 0 and 1) the fractions of the length around the profile and
// along the path that the mesh stores in uv times those lengths (p[9].x around the outer ring,
// p[9].y around the holes, whose u starts at 2, and p[9].z along the path), on the caps
// (regions 8 and 9) the profile x and y in uv times the scale p[2].xy. Reads the surface_data
// of surface.wgsl; box_surface_build_lengths and box_surface_profile_length of
// src/geometry/surface_math.c compute the lengths.
fn surface_sweep_lengths(uv: vec2f, region: i32) -> vec2f {
    let p = surface_data.p;
    var result = uv * p[2].xy;
    if (region == 0) {
        result = uv * p[9].xz;
    } else if (region == 1) {
        result = vec2f((uv.x - 2.0) * p[9].y, uv.y * p[9].z);
    }
    return result;
}

// Texture coordinates of a fragment of a MappingObject or MappingWorld material, projected onto
// the plane of the dominant axis of its object (or world) normal, oriented as the faces of a
// Box: on the side faces u runs along the horizontal tangent seen from outside (up x normal) and
// v toward -y; the top has u along +z and the bottom along -z, both v along -x. object comes
// from mapping_object, offset from mapping_offset. It does not branch, so a texture sample after
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

// Albedo alpha of an alpha tested fragment from albedo (its albedo layer), at the interpolated
// UV, or for a MappingObject or MappingWorld material (mapped not 0) projected with mapping_uv,
// uv its offset. With a blend (mapped above 1) it blends the alpha of each projection with a
// weight, sampled with the gradients of its projection from the screen derivatives of object,
// once for a fragment whose normal is along an axis. mapped is flat, the same for every
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
