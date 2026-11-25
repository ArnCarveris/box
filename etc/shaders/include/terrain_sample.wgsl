// The height, normal and masks of a Terrain with TerrainSampled (box.terrain) by world position,
// for compute and vertex stages, read from the tiles the terrain draws: the heights of each tile
// in its slot of the surface assets of the grid mesh (terrain.wgsl) and its masks in its cell of
// the mask atlas (terrain_mask of mesh_blend.wgsl). A grid per level, centered on the first
// focus, holds the slot of the tile that draws at each node of the level, so a sample answers
// from the finest tile that draws at the point: the finest levels around the focus and the
// coarser ones further out. A point outside every window, outside a bounded terrain or where no
// tile draws yet is not found.
//
// Positions are world x and z, whole meters and the rest of a meter (terrain_sample), so they
// stay continuous across moves of the render origin and precise far from the origin of the
// world; a position relative to the render origin is the origin plus it (terrain_sample_local).
//
// The bindings are those of terrain_sample_layout (box_gpu_terrain_sample_group) at the group
// TERRAIN_SAMPLE_GROUP, 0 unless the includer defines it before the include.
#pragma once
#include "render_origin.wgsl"

#ifndef TERRAIN_SAMPLE_GROUP
#define TERRAIN_SAMPLE_GROUP 0
#endif

// Texels of the heights beyond each edge of a tile (BOX_TERRAIN_BORDER)
const TERRAIN_SAMPLE_BORDER: f32 = 2.0;

// A level of the windows (box_gpu_terrain_sample_level_t)
struct TerrainSampleLevel {
    whole: vec2i,       // Absolute x and z of the corner of the window, whole meters
    rest: vec2f,        // The rest of the corner below a meter
    size: f32,          // Edge of a tile of the level
    morph_start: f32,   // Distance of the focus from the center of a tile where its morph starts
    morph_end: f32,     // Where it ends
    first: u32,         // First cell of the level in terrain_sample_grid
};

// The params of a sampled terrain (box_gpu_terrain_sample_params_t)
struct TerrainSampleParams {
    counts: vec4u,          // Levels, cells along an edge of a window, vertices along an edge
                            // of a tile, foci; no levels before a tile draws
    strides: vec4u,         // Floats of a row of heights, floats of a slot, texels along an edge
                            // of the masks of a tile (0 without masks), cells across the atlas
    origin_whole: vec4i,    // Absolute x and z of the frame of the terrain, whole meters, then 1
                            // when the terrain is bounded
    origin: vec4f,          // The rest of x and z, the extent of a bounded terrain and the local
                            // y of the frame of the terrain
    foci: array<vec4f, 4>,  // Two foci per vec4, x and z from the corner of the window of level 0
    levels: array<TerrainSampleLevel, 16>,
};

@group(TERRAIN_SAMPLE_GROUP) @binding(0) var<uniform> terrain_sample_params: TerrainSampleParams;
// Slot of the tile that draws at each cell of the windows plus one, 0 for none
@group(TERRAIN_SAMPLE_GROUP) @binding(1) var<storage, read> terrain_sample_grid: array<u32>;
@group(TERRAIN_SAMPLE_GROUP) @binding(2) var<storage, read> terrain_sample_heights: array<vec4f>;
@group(TERRAIN_SAMPLE_GROUP) @binding(3) var terrain_sample_masks: texture_2d<f32>;

// The tile that answers at a point
struct TerrainSampleTile {
    found: bool,        // A tile draws at the point
    slot: u32,          // Its slot
    level: u32,         // Its level
    t: vec2f,           // The point in texels of its heights from their first texel
    morph: f32,         // Its morph into the grid of the next coarser level at the point, 0..1
    size: f32,          // Its edge in meters
};

// What a terrain has at a point
struct TerrainSample {
    found: bool,        // A tile draws at the point; else the rest is zero and the normal up
    level: u32,         // Level of the tile that answered
    height: f32,        // Height in meters in the frame of the terrain, as box_terrain_height
    y: f32,             // Local world y of the surface
    normal: vec3f,      // Unit normal
    mask: vec4f,        // The four channels of the masks node, zero without masks
};

// Find the finest tile that draws at an absolute position: whole meters and the rest, which
// may be any value.
fn terrain_sample_find(whole: vec2i, rest: vec2f) -> TerrainSampleTile {
    var result: TerrainSampleTile;
    let counts = terrain_sample_params.counts;
    let cells = i32(counts.y);
    let n = f32(counts.z) - 1.0;
    if (counts.x == 0u) {
        return result;
    }

    if (terrain_sample_params.origin_whole.z != 0) {
        let q = vec2f(whole - terrain_sample_params.origin_whole.xy) +
            (rest - terrain_sample_params.origin.xy);
        if (any(abs(q) > vec2f(terrain_sample_params.origin.z))) {
            return result;
        }
    }

    let level0 = terrain_sample_params.levels[0];
    for (var l = 0u; l < counts.x; l++) {
        let level = terrain_sample_params.levels[l];
        let d = vec2f(whole - level.whole) + (rest - level.rest);
        let c = vec2i(floor(d / level.size));
        if (any(c < vec2i(0)) || any(c >= vec2i(cells))) {
            continue;
        }

        let v = terrain_sample_grid[level.first + u32(c.y * cells + c.x)];
        if (v == 0u) {
            continue;
        }

        // The tile morphs around the focus nearest to its center (box_terrain_tile_write)
        let base = vec2f(level.whole - level0.whole) + (level.rest - level0.rest);
        let center = base + (vec2f(c) + vec2f(0.5)) * level.size;
        var focus = center;
        var best = 3.4e38;
        for (var i = 0u; i < counts.w; i++) {
            let pair = terrain_sample_params.foci[i / 2u];
            let f = select(pair.xy, pair.zw, (i & 1u) == 1u);
            let e = dot(f - center, f - center);
            if (e < best) {
                best = e;
                focus = f;
            }
        }

        let span = max(level.morph_end - level.morph_start, 1e-4);
        let away = length(base + d - focus);
        result.found = true;
        result.slot = v - 1u;
        result.level = l;
        result.t = (d - vec2f(c) * level.size) / level.size * n + vec2f(TERRAIN_SAMPLE_BORDER);
        result.morph = select(0.0, clamp((away - level.morph_start) / span, 0.0, 1.0),
            counts.w != 0u);
        result.size = level.size;
        return result;
    }

    return result;
}

// Height of a texel of the heights of a slot, clamped to the image.
fn terrain_sample_texel(slot: u32, x: i32, z: i32) -> f32 {
    let w = i32(terrain_sample_params.counts.z) + 2 * i32(TERRAIN_SAMPLE_BORDER);
    let cx = u32(clamp(x, 0, w - 1));
    let cz = u32(clamp(z, 0, w - 1));
    let i = slot * terrain_sample_params.strides.y + cz * terrain_sample_params.strides.x + cx;
    return terrain_sample_heights[i >> 2u][i & 3u];
}

// Height of a slot at t, in texels from its first texel, bilinear between the texels.
fn terrain_sample_bilinear(slot: u32, t: vec2f) -> f32 {
    let f = floor(t);
    let a = t - f;
    let x = i32(f.x);
    let z = i32(f.y);
    let top = mix(terrain_sample_texel(slot, x, z), terrain_sample_texel(slot, x + 1, z), a.x);
    let bottom = mix(terrain_sample_texel(slot, x, z + 1),
        terrain_sample_texel(slot, x + 1, z + 1), a.x);
    return mix(top, bottom, a.y);
}

// Height of a slot at t on the triangles of its grid with vertices step texels apart (1 the grid
// of the tile, 2 that of the next coarser level), each cell split from its lower to its higher
// corner as the grid mesh is (box_terrain_tile_generate), so it is the surface as drawn.
fn terrain_sample_triangles(slot: u32, t: vec2f, step: i32) -> f32 {
    let g = (t - vec2f(TERRAIN_SAMPLE_BORDER)) / f32(step);
    let c = floor(g);
    let a = g - c;
    let x = i32(c.x) * step + i32(TERRAIN_SAMPLE_BORDER);
    let z = i32(c.y) * step + i32(TERRAIN_SAMPLE_BORDER);
    let h00 = terrain_sample_texel(slot, x, z);
    let h11 = terrain_sample_texel(slot, x + step, z + step);
    if (a.x >= a.y) {
        let h10 = terrain_sample_texel(slot, x + step, z);
        return h00 + a.x * (h10 - h00) + a.y * (h11 - h10);
    }

    let h01 = terrain_sample_texel(slot, x, z + step);
    return h00 + a.y * (h01 - h00) + a.x * (h11 - h01);
}

// Height of a tile at its point in the frame of the terrain: on its grid, faded into the grid of
// the next coarser level by its morph as the vertex stage morphs it (terrain_vertex).
fn terrain_sample_tile_height(tile: TerrainSampleTile) -> f32 {
    let fine = terrain_sample_triangles(tile.slot, tile.t, 1);
    if (tile.morph <= 0.0) {
        return fine;
    }

    return mix(fine, terrain_sample_triangles(tile.slot, tile.t, 2), tile.morph);
}

// Normal of a tile at its point, of central differences one vertex step apart, two at a full
// morph, as terrain_vertex takes them.
fn terrain_sample_tile_normal(tile: TerrainSampleTile) -> vec3f {
    let reach = 1.0 + tile.morph;
    let dx = terrain_sample_bilinear(tile.slot, tile.t + vec2f(reach, 0.0)) -
        terrain_sample_bilinear(tile.slot, tile.t - vec2f(reach, 0.0));
    let dz = terrain_sample_bilinear(tile.slot, tile.t + vec2f(0.0, reach)) -
        terrain_sample_bilinear(tile.slot, tile.t - vec2f(0.0, reach));
    let cell = tile.size / max(f32(terrain_sample_params.counts.z) - 1.0, 1.0);
    let scale = 1.0 / (2.0 * reach * cell);
    return normalize(vec3f(-dx * scale, 1.0, -dz * scale));
}

// A texel of the masks of a tile in the mask atlas: origin the corner of the image in the atlas,
// size its texels along an edge, p clamped to the image.
fn terrain_sample_mask_texel(origin: vec2i, size: i32, p: vec2i) -> vec4f {
    return textureLoad(terrain_sample_masks, origin + clamp(p, vec2i(0), vec2i(size - 1)), 0);
}

// The masks of a tile bilinear between its texels at t, in texels from the corner.
fn terrain_sample_mask_bilinear(origin: vec2i, size: i32, t: vec2f) -> vec4f {
    let f = floor(t);
    let a = t - f;
    let p = vec2i(f);
    let top = mix(terrain_sample_mask_texel(origin, size, p),
        terrain_sample_mask_texel(origin, size, p + vec2i(1, 0)), a.x);
    let bottom = mix(terrain_sample_mask_texel(origin, size, p + vec2i(0, 1)),
        terrain_sample_mask_texel(origin, size, p + vec2i(1, 1)), a.x);
    return mix(top, bottom, a.y);
}

// The masks of a tile at its point, faded into those of the next coarser level by its morph as
// the fragment stage fades them (terrain_mask of mesh_blend.wgsl); zero without masks.
fn terrain_sample_tile_mask(tile: TerrainSampleTile) -> vec4f {
    let texels = i32(terrain_sample_params.strides.z);
    if (texels == 0) {
        return vec4f(0.0);
    }

    let columns = max(terrain_sample_params.strides.w, 1u);
    let origin = vec2i(i32(tile.slot % columns) * 2 * texels, i32(tile.slot / columns) * texels);
    let fine = terrain_sample_mask_bilinear(origin, texels, tile.t);
    if (tile.morph <= 0.0) {
        return fine;
    }

    let coarse = terrain_sample_mask_bilinear(origin + vec2i(texels, 0), (texels + 1) / 2,
        tile.t * 0.5);
    return mix(fine, coarse, tile.morph);
}

// What the terrain has at an absolute position: whole meters and the rest, which may be any
// value.
fn terrain_sample(whole: vec2i, rest: vec2f) -> TerrainSample {
    var result: TerrainSample;
    result.normal = vec3f(0.0, 1.0, 0.0);
    let tile = terrain_sample_find(whole, rest);
    if (!tile.found) {
        return result;
    }

    result.found = true;
    result.level = tile.level;
    result.height = terrain_sample_tile_height(tile);
    result.y = result.height + terrain_sample_params.origin.w;
    result.normal = terrain_sample_tile_normal(tile);
    result.mask = terrain_sample_tile_mask(tile);
    return result;
}

// The height in the frame of the terrain at an absolute position and 1 where a tile draws, else
// zero, without the normal and the masks.
fn terrain_sample_height(whole: vec2i, rest: vec2f) -> vec2f {
    let tile = terrain_sample_find(whole, rest);
    if (!tile.found) {
        return vec2f(0.0);
    }

    return vec2f(terrain_sample_tile_height(tile), 1.0);
}

// What the terrain has at an absolute x and z in f32, as precise as f32 is that far from the
// absolute origin.
fn terrain_sample_world(xz: vec2f) -> TerrainSample {
    let w = floor(xz);
    return terrain_sample(vec2i(w), xz - w);
}

// What the terrain has at an x and z relative to the render origin (View.origin,
// render_origin).
fn terrain_sample_local(origin: RenderOrigin, xz: vec2f) -> TerrainSample {
    return terrain_sample(origin.whole.xz, origin.rest.xz + xz);
}
