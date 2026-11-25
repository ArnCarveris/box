// What the place pass (grass_place.wgsl) and the draw (grass.wgsl) of the grass blades share: the
// params of a Grass in a view, a blade, the levels of detail, the hash of the cells and the bend
// of a blade in the wind, which the place pass makes and the draw reads.
//
// A blade belongs to a cell of a grid of absolute world cells and takes its place, size, facing
// and shade from a hash of the cell and the seed, and of the clump of a voronoi pattern over the
// world it is in, so it stays where it is as the camera and the render origin move. The blades
// are drawn as triangle strips of
// 2 * segments + 1 vertices built from the vertex index, the level of a draw in its first vertex.
#pragma once

#include "grass_wind.wgsl"

// Levels of detail (BOX_GRASS_LODS)
const GRASS_LODS: u32 = 3u;

// Vertex indices a level of detail takes, its first vertex is its level times this
// (BOX_GPU_GRASS_LOD_VERTICES)
const GRASS_LOD_VERTICES: u32 = 64u;

// Thinning levels: past the level of a block the blade of a cell does not stand for more blades
const GRASS_THIN_LEVELS: u32 = 6u;

// Largest factor a blade widens by where the field thins
const GRASS_WIDEN_MAX: f32 = 32.0;

// The params of a Grass in a view (box_gpu_grass_params_t), relative to the render origin
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
                            // over the world, height below which blades fold into a V, 0 for none
    pixel: vec4f,           // Size of a pixel per meter of distance (perspective) and at any
                            // distance (orthographic), one of them 0
    wind: GrassWind,        // The Wind of the frame (grass_wind.wgsl)
    light: vec4f,           // The light the blades cast shadows of: xyz the direction toward a
                            // directional light or the local position of a spot or point
                            // light; w 0 directional, 1 spot, 2 plus the octant of a point
                            // light tile
    planes: vec4f,          // Near and far plane of a point light tile, zw 0
    caster: vec4f,          // Distance the blades cast shadows within, 0 for none; distance
                            // their shadows start to fade at; how far along the light a
                            // placement of the blades that cast sweeps the frustum; 1 for that
                            // placement, 0 for the one of the view
    hiz: vec4u,             // Width, height and levels of the depth pyramid of the view the
                            // placement culls by occlusion against, 0 levels for none
};

// A blade (box_gpu_grass_blade_t), 64 bytes
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
                            // in RGB8 (pack4x8unorm); in A the phase of the swing of its clump
                            // in the high 4 bits, its stiffness in the wind in the low 4 bits
    clump: f32,             // Distance from the center of its clump in clump edges
    wind: vec4f,            // Its bend in the wind of the frame (grass_wind_bend), made as it
                            // is placed, so its vertices do not each make it again
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

// Bits of the wind of a blade in the high byte of its tint: the phase of its clump in sixteenths
// and its stiffness in fifteenths.
fn grass_wind_bits(phase: f32, stiffness: f32) -> u32 {
    let p = min(u32(phase * 16.0), 15u);
    let s = u32(round(saturate(stiffness) * 15.0));
    return ((p << 4u) | s) << 24u;
}

// A hash as a value from 0 to 1.
fn grass_unit(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

// Share of the bend of a blade a stiff blade keeps
const WIND_STIFF: f32 = 0.4;

// Swing in radians of a clump along the wind at full wind, and across it as a share of that
const WIND_SWING: f32 = 0.18;
const WIND_SWING_CROSS: f32 = 0.5;

// Flutter in radians of a blade at full wind, of which WIND_FLUTTER_STEADY flutters without a gust
const WIND_FLUTTER: f32 = 0.1;
const WIND_FLUTTER_STEADY: f32 = 0.3;

// Twist in radians of the tip of a blade around its length as it flutters at full wind
const WIND_TWIST: f32 = 0.35;

// Swing and flutter in still air, as a share of those at full wind
const WIND_CALM: f32 = 0.2;

// The bend of a blade in the wind: xy the horizontal direction and angle in radians its tip turns
// toward about its root, z the angle its tip twists by around its length, w the angle the gust or
// lull where it stands adds to the lean of the steady wind, below 0 in a lull.
fn grass_wind_bend(b: GrassBlade) -> vec4f {
    let w = params.wind;
    let wave = grass_wind_wave(b.position.xz, w);
    let bits = b.tint >> 24u;
    let stiffness = f32(bits & 15u) / 15.0;
    let clump = f32(bits >> 4u) / 16.0;
    let own = grass_unit(grass_hash(bitcast<u32>(b.yaw)));
    let free = mix(1.0, WIND_STIFF, stiffness);
    let full = clamp(w.strength.x / GRASS_WIND_FULL, 0.0, 1.0);
    let a = GRASS_WIND_TAU * w.time.x;
    let dir = w.field.xy;
    let across = vec2f(-dir.y, dir.x);

    // The lean toward the wave
    let strength = length(wave.xy);
    let lean = grass_wind_lean(strength);
    var r = vec2f(0.0);
    if (strength > 0.0) {
        r = wave.xy * (lean / strength);
    }

    // The clump swings around the lean as the gust stirs it, faintly in still air
    let swing = WIND_SWING * (full * wave.w + WIND_CALM * (1.0 - full));
    let sp = GRASS_WIND_TAU * clump;
    r += dir * (swing * sin(a + sp)) +
        across * (swing * WIND_SWING_CROSS * sin(2.0 * a + 1.3 + sp));

    // The blade flutters at its own phase, more in a gust
    let flutter = WIND_FLUTTER * (full * mix(WIND_FLUTTER_STEADY, 1.0, wave.w) +
        WIND_CALM * (1.0 - full));
    let fp = GRASS_WIND_TAU * own;
    r += vec2f(sin(5.0 * a + fp), sin(7.0 * a + 1.7 * fp + 2.0)) * flutter;
    let twist = WIND_TWIST / WIND_FLUTTER * flutter * sin(6.0 * a + fp + 1.0);
    let gust = lean - grass_wind_lean(w.strength.x / GRASS_WIND_FULL);
    return vec4f(r * free, twist * free, gust * free);
}
