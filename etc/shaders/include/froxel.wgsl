// Froxel grid of the fog volumes of a view (gpu_fog_volume.c): grid.x by grid.y cells, one per
// tile of range.x pixels of the render size, by grid.z depth slices from the near plane of the
// camera (range.y) to range.z, fog_distance beyond it. A perspective camera spaces the slices
// exponentially (BOX_FROXEL_EXPONENT), an orthographic one linearly. Depths are view depths, the
// distance in front of the camera along its axis, so a slice is a plane parallel to the image.
//
// The injected grid is temporal: the cells of the previous frame of the view are found through
// its slicing, prev_depth_plane and prev_range.
//
// The includer declares the Froxel uniform as froxel.
#pragma once

#include "froxel_share.wgsl"

struct Froxel {
    // Clip space of the target to world, as View.inv_view_proj
    inv_view_proj: mat4x4f,
    // Plane whose dot product with a world position (w 1) is its view depth
    depth_plane: vec4f,
    // x, y: cells across and down, z: slices, w: fog volumes of the view
    grid: vec4u,
    // x: edge of the tile of a cell in pixels, y: view depth of the near plane, z: view depth of
    // the far end of the grid, w: 1 for an orthographic camera
    range: vec4f,
    // x, y: render width and height in pixels, z, w: the share of the grid they cover across and
    // down, width / (grid.x * tile) and height / (grid.y * tile)
    size: vec4f,
    // x: segment of the rays an apply draw takes, SEGMENT_* of fog_volume.wgsl
    segment: vec4u,
    // depth_plane of the previous frame
    prev_depth_plane: vec4f,
    // x: view depth of the near plane and y: of the far end of the grid in the previous frame,
    // z: 1 for an orthographic camera then, w: 1 when inject blends with the previous cells
    prev_range: vec4f,
};

// A ray of the grid through a point of the image: the world position at view depth 0 and the
// world offset per unit of view depth, whose length is the distance a slice of unit thickness
// takes along the ray.
struct FroxelRay {
    origin: vec3f,
    dir: vec3f,
};

// World position of a uv of the image, (0, 0) at its top left, at a clip depth.
fn froxel_world(uv: vec2f, depth: f32) -> vec3f {
    let p = froxel.inv_view_proj * vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    return p.xyz / p.w;
}

// View depth of a world position.
fn froxel_view_depth(p: vec3f) -> f32 {
    return dot(froxel.depth_plane, vec4f(p, 1.0));
}

// Ray of the grid through a uv of the image, from two points of it in reversed Z: the near plane
// at 1 and a farther one at 0.5, finite with an infinite far plane too.
fn froxel_ray(uv: vec2f) -> FroxelRay {
    let a = froxel_world(uv, 1.0);
    let b = froxel_world(uv, 0.5);
    let da = froxel_view_depth(a);
    let db = froxel_view_depth(b);
    var r: FroxelRay;
    r.dir = (b - a) / select(db - da, 1e-6, abs(db - da) < 1e-6);
    r.origin = a - r.dir * da;
    return r;
}

// Share of the depth of the grid, 0 at the near plane and 1 at its far end, of a view depth,
// clamped to 0..1.
fn froxel_share(depth: f32) -> f32 {
    return froxel_share_of(depth, froxel.range.y, froxel.range.z, froxel.range.w > 0.5);
}

// View depth at a share of the depth of the grid, the inverse of froxel_share.
fn froxel_depth(share: f32) -> f32 {
    var x = share;
    if (froxel.range.w < 0.5) {
        x = (exp2(BOX_FROXEL_EXPONENT * share) - 1.0) / (exp2(BOX_FROXEL_EXPONENT) - 1.0);
    }

    return mix(froxel.range.y, froxel.range.z, x);
}

// Share of the depth of a cell, from its near to its far side, of the sample point j of
// BOX_FROXEL_SAMPLES, offset inside its stratum by 0..1, 0.5 at its middle.
fn froxel_sample(j: u32, offset: f32) -> f32 {
    return (f32(j) + offset) / f32(BOX_FROXEL_SAMPLES);
}

// uv of the image at the center of a cell.
fn froxel_cell_uv(cell: vec2u) -> vec2f {
    return froxel_cell_point_uv(cell, vec2f(0.5));
}

// uv of the image at a point of a cell, offset from its top left corner by 0..1 of its size.
fn froxel_cell_point_uv(cell: vec2u, offset: vec2f) -> vec2f {
    return (vec2f(cell) + offset) * froxel.range.x / froxel.size.xy;
}
