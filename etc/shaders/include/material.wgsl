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
    // TextureMapping.mode: 0 the UVs of the mesh, 1 projected from the object position
    mapping: u32,
    // TextureMapping.size of a MappingObject material, world units per repeat; 0 for the UVs
    mapping_size: f32,
};

// Record of a material id; an id past the table reads the default material of slot 0.
fn material_fetch(id: u32) -> Material {
    return materials[select(0u, id, id < arrayLength(&materials))];
}

// The material projects its texture coordinates from the object position (MappingObject).
fn material_mapped(m: Material) -> bool {
    return m.mapping == 1u;
}

// What a vertex of a MappingObject material passes on for the projection of its fragments: its
// object position times the scale of its instance (the lengths of the model matrix columns) over
// the mapping size, with the TextureTransform scale of the texture axis each one maps to:
// (x * su, y * sv, z * su, x * sv). It is linear in the position, so it interpolates exactly.
fn mapping_object(m: Material, position: vec3f, scale: vec3f) -> vec4f {
    let p = position * scale / m.mapping_size;
    return vec4f(p.x * m.uv.x, p.y * m.uv.y, p.z * m.uv.x, p.x * m.uv.y);
}

// What mapping_uv adds after the projection: the TextureTransform offset, and half the scale,
// so a unit cube with a size of 1 maps as its UVs do.
fn mapping_offset(m: Material) -> vec2f {
    return 0.5 * m.uv.xy + m.uv.zw;
}

// Texture coordinates of a fragment of a MappingObject material, projected onto the plane of
// the dominant axis of its object normal, oriented as the faces of a Box: on the side faces u
// runs along the horizontal tangent seen from outside (up x normal) and v toward -y; the top
// has u along +z and the bottom along -z, both v along -x. object comes from mapping_object,
// offset from mapping_offset. It does not branch, so a texture sample after it stays in
// uniform control flow.
fn mapping_uv(object: vec4f, normal: vec3f, offset: vec2f) -> vec2f {
    let a = abs(normal);
    let s = select(vec3f(-1.0), vec3f(1.0), normal >= vec3f(0.0));
    let side_x = vec2f(-s.x * object.z, -object.y);
    let side_z = vec2f(s.z * object.x, -object.y);
    let top = vec2f(s.y * object.z, -object.w);
    let side = select(side_z, side_x, a.x >= a.z);
    return select(side, top, a.y >= max(a.x, a.z)) + offset;
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
