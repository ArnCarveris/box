// What the place pass (grass_place.wgsl) and the draw (grass.wgsl) of the grass blades share: the
// params of a Grass in a view, a blade, the levels of detail and the hash of the cells.
//
// A blade belongs to a cell of a grid of absolute world cells and takes its place, size, facing
// and shade from a hash of the cell and the seed, and of the clump of a voronoi pattern over the
// world it is in, so it stays where it is as the camera moves and across shifts of the
// WorldOrigin. The blades are drawn as triangle strips of
// 2 * segments + 1 vertices built from the vertex index, the level of a draw in its first vertex.
#pragma once

// Levels of detail (BOX_GRASS_LODS)
const GRASS_LODS: u32 = 3u;

// Vertex indices a level of detail takes, its first vertex is its level times this
// (BOX_GPU_GRASS_LOD_VERTICES)
const GRASS_LOD_VERTICES: u32 = 64u;

// Thinning levels: past the level of a block the blade of a cell does not stand for more blades
const GRASS_THIN_LEVELS: u32 = 6u;

// Largest factor a blade widens by where the field thins
const GRASS_WIDEN_MAX: f32 = 32.0;

// The params of a Grass in a view (box_gpu_grass_params_t), in the local frame of the packet
struct GrassParams {
    view_proj: mat4x4f,     // The view projection of the view
    camera: vec4f,          // Local position of the camera, w the distance the blades end at
    patches: vec4i,         // Absolute x and z of the patch at the corner of the grid, patches
                            // along an edge of the grid, cells along an edge of a patch
    corner: vec4f,          // Local x and z of the corner of the grid, edge of a patch in whole
                            // meters, edge of a cell
    ground: vec4f,          // Lowest and highest local y of the ground, local y of the plane,
                            // distance the thinning starts at
    lod: vec4f,             // Distances levels 0 and 1 end at, the blades end at, they start to
                            // fade at
    blade: vec4f,           // Shortest and tallest blade, narrowest and widest
    base: vec4f,            // Linear color at the base
    tip: vec4f,             // Linear color at the tip
    config: vec4u,          // Mask channel plus one, 0 for none; 1 on a terrain; seed; blades
                            // a level holds at most
    shape: vec4f,           // Edge of a clump, how strongly blades follow their clump, variation
                            // over the world
    pixel: vec4f,           // Size of a pixel per meter of distance (perspective) and at any
                            // distance (orthographic), one of them 0
};

// A blade (box_gpu_grass_blade_t), 48 bytes
struct GrassBlade {
    position: vec3f,        // Local position of its base
    yaw: f32,               // Angle of its facing around the vertical
    height: f32,            // Height
    width: f32,             // Width at the base
    lean: f32,              // How far its tip leans along its facing, 0 to 1
    shade: f32,             // Brightness of its color
    bend: f32,              // How far it bends over toward its tip, 0 to 1
    curl: f32,              // Curl around its length, -1 to 1
    tint: u32,              // Factor of its color from the variation over the world, halved,
                            // in RGBA8 (pack4x8unorm)
    clump: f32,             // Distance from the center of its clump in clump edges
};

// Size in meters of a pixel at a distance from the camera.
fn grass_pixel(d: f32) -> f32 {
    return params.pixel.x * d + params.pixel.y;
}

// Segments of a blade of a level: 7, 3 and a single triangle.
fn grass_segments(lod: u32) -> u32 {
    return select(select(1u, 3u, lod == 1u), 7u, lod == 0u);
}

// Vertices of the strip of a blade of a level.
fn grass_vertices(lod: u32) -> u32 {
    return 2u * grass_segments(lod) + 1u;
}

// Hash of a word (PCG).
fn grass_hash(x: u32) -> u32 {
    let state = x * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Hash of an absolute cell with a salt and a seed.
fn grass_cell_hash(c: vec2i, salt: u32, seed: u32) -> u32 {
    return grass_hash(bitcast<u32>(c.x) ^ grass_hash(bitcast<u32>(c.y) ^ grass_hash(salt ^ seed)));
}

// A hash as a value from 0 to 1.
fn grass_unit(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}
