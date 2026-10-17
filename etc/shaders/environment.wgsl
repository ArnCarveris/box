// Environment image conversion: writes an equirectangular image into mip 0 of the cubemap of an
// environment source, six layers in WebGPU cube order, the texel directions of cube_direction.
//
// Column u of the image is the longitude and row v the latitude: direction d is at
// u = 0.5 + atan2(d.x, -d.z) / 2 pi and v = acos(d.y) / pi, so the center column looks along -Z,
// the image goes toward +X to its right, its edges look along +Z and its top row is +Y. Each
// texel averages four samples at the centers of its quarters, so a large image does not alias.

#include "include/cube.wgsl"

const PI: f32 = 3.141592653589793;

struct Params {
    // x: face edge of the cubemap in texels
    size: vec4u,
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var equirect: texture_2d<f32>;
@group(0) @binding(2) var equirect_sampler: sampler;
@group(0) @binding(3) var cube: texture_storage_2d_array<rgba16float, write>;

// Where the image holds direction d.
fn equirect_uv(d: vec3f) -> vec2f {
    let u = 0.5 + atan2(d.x, -d.z) / (2.0 * PI);
    let v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return vec2f(u, v);
}

// Direction of a point of face f at texel coordinates p, p = texel + 0.5 at the texel center,
// as cube_direction computes it.
fn face_direction(face: u32, p: vec2f, size: u32) -> vec3f {
    let step = 2.0 / f32(size);
    let axes = face_axes(face);
    let right = cross(axes[0], axes[1]);
    return normalize(axes[0] + right * (1.0 - p.x * step) + axes[1] * (1.0 - p.y * step));
}

@compute @workgroup_size(8, 8)
fn equirect_to_cube(@builtin(global_invocation_id) id: vec3u) {
    let size = max(params.size.x, 1u);
    if (id.x >= size || id.y >= size || id.z >= 6u) {
        return;
    }

    var sum = vec3f(0.0);
    for (var i = 0u; i < 4u; i++) {
        let offset = vec2f(f32(i & 1u), f32(i >> 1u)) * 0.5 + vec2f(0.25);
        let d = face_direction(id.z, vec2f(id.xy) + offset, size);
        sum += textureSampleLevel(equirect, equirect_sampler, equirect_uv(d), 0.0).rgb;
    }

    textureStore(cube, id.xy, id.z, vec4f(sum * 0.25, 1.0));
}
