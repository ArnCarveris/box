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
    reserved: u32,
    // TextureTransform: u scale, v scale, u offset, v offset
    uv: vec4f,
};

// Record of a material id; an id past the table reads the default material of slot 0.
fn material_fetch(id: u32) -> Material {
    return materials[select(0u, id, id < arrayLength(&materials))];
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
