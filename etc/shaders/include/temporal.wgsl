// Temporal foundation of the passes that read the previous frame (gpu_history.c): where a world
// position was in the previous frame of the view, whether that frame is the history of this one,
// and low discrepancy sequences of the frame index that are the same on every run. The shader
// that includes it binds the View as `view`.
#pragma once

#include "lights.wgsl"

// Frames the sequences cycle over, BOX_RENDER_TEMPORAL_LENGTH of render_backend.h
const TEMPORAL_SEQUENCE_LENGTH: u32 = 16u;

// Steps of the R2 sequence: 1 / g and 1 / g^2 of the plastic number g
const TEMPORAL_R2: vec2f = vec2f(0.7548776662466927, 0.5698402909980532);

// Whether the previous frame of the view is the history of this one: false on the first frame
// of the view, after a resize, a new camera entity or projection, and after a HistoryReset.
fn history_valid() -> bool {
    return view.temporal.y != 0u;
}

// Frames the view rendered since the last reset of its history, 0 on a reset.
fn temporal_frame() -> u32 {
    return view.temporal.x;
}

// Where a world position was in the previous frame: xy its uv (0..1, y down) and z its clip
// depth (reversed Z, 1 at the near plane), through the previous view projection of a perspective
// or orthographic camera. A position behind the previous camera lands outside the image.
fn reproject(world: vec3f) -> vec3f {
    let clip = view.prev_view_proj * vec4f(world, 1.0);
    if (clip.w <= 1e-6) {
        return vec3f(-1.0, -1.0, 0.0);
    }

    let ndc = clip.xyz / clip.w;
    return vec3f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

// Whether a reprojected position is inside the previous image and its depth range.
fn reproject_inside(p: vec3f) -> bool {
    return all(p.xy >= vec2f(0.0)) && all(p.xy <= vec2f(1.0)) && p.z >= 0.0 && p.z <= 1.0;
}

// The world position of a uv (0..1, y down) and clip depth of the current frame.
fn temporal_world(uv: vec2f, depth: f32) -> vec3f {
    let p = view.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return p.xyz / p.w;
}

// Radical inverse of an index in a base: its digits mirrored around the radix point.
fn temporal_radical_inverse(index: u32, base: u32) -> f32 {
    let inv = 1.0 / f32(base);
    var n = index;
    var f = inv;
    var result = 0.0;
    while (n > 0u) {
        result += f * f32(n % base);
        n /= base;
        f *= inv;
    }

    return result;
}

// Point of the R2 sequence of a frame index, in [0, 1)^2, cycling over
// TEMPORAL_SEQUENCE_LENGTH frames.
fn temporal_sequence_2d_at(index: u32) -> vec2f {
    let n = f32(index % TEMPORAL_SEQUENCE_LENGTH);
    return fract(vec2f(0.5) + n * TEMPORAL_R2);
}

// Point of the Halton 2, 3, 5 sequence of a frame index, in (0, 1)^3, cycling over
// TEMPORAL_SEQUENCE_LENGTH frames; it starts at index 1 so no point is at the origin.
fn temporal_sequence_3d_at(index: u32) -> vec3f {
    let n = index % TEMPORAL_SEQUENCE_LENGTH + 1u;
    return vec3f(temporal_radical_inverse(n, 2u), temporal_radical_inverse(n, 3u),
        temporal_radical_inverse(n, 5u));
}

// The 2D sequence point of the current frame of the view.
fn temporal_sequence_2d() -> vec2f {
    return temporal_sequence_2d_at(view.temporal.x);
}

// The 3D sequence point of the current frame of the view.
fn temporal_sequence_3d() -> vec3f {
    return temporal_sequence_3d_at(view.temporal.x);
}

// Interleaved gradient noise of a pixel (Jimenez 2014), in [0, 1).
fn temporal_noise(pixel: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(floor(pixel), vec2f(0.06711056, 0.00583715))));
}

// The 2D sequence point of the current frame shifted by the noise of a pixel, so neighboring
// pixels sample different points of the sequence in every frame.
fn temporal_interleaved_2d(pixel: vec2f) -> vec2f {
    let n = vec2f(temporal_noise(pixel), temporal_noise(pixel + vec2f(5.588238, 3.17)));
    return fract(temporal_sequence_2d() + n);
}

// The 3D sequence point of the current frame shifted by the noise of a pixel.
fn temporal_interleaved_3d(pixel: vec2f) -> vec3f {
    let n = vec3f(temporal_noise(pixel), temporal_noise(pixel + vec2f(5.588238, 3.17)),
        temporal_noise(pixel + vec2f(11.3, 7.71)));
    return fract(temporal_sequence_3d() + n);
}
