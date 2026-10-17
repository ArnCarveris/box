// Cubemap face directions shared by the probe captures and the environment sources. Face f is
// in WebGPU cube order: +X, -X, +Y, -Y, +Z, -Z.
#pragma once

// Axis a face looks along and its up axis.
fn face_axes(face: u32) -> mat2x3f {
    switch face {
        case 0u: { return mat2x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0)); }
        case 1u: { return mat2x3f(vec3f(-1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0)); }
        case 2u: { return mat2x3f(vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, -1.0)); }
        case 3u: { return mat2x3f(vec3f(0.0, -1.0, 0.0), vec3f(0.0, 0.0, 1.0)); }
        case 4u: { return mat2x3f(vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0)); }
        default: { return mat2x3f(vec3f(0.0, 0.0, -1.0), vec3f(0.0, 1.0, 0.0)); }
    }
}

// Direction texel (x, y) of a cubemap face looks along in the WebGPU cubemap convention: the
// face axes of the atlas with u mirrored, as the store mirrors the faces.
fn cube_direction(face: u32, texel: vec2u, size: u32) -> vec3f {
    let step = 2.0 / f32(size);
    let axes = face_axes(face);
    let right = cross(axes[0], axes[1]);
    let u = 1.0 - (f32(texel.x) + 0.5) * step;
    let v = 1.0 - (f32(texel.y) + 0.5) * step;
    return normalize(axes[0] + right * u + axes[1] * v);
}
