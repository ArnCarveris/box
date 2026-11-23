// The bake of the material of a terrain tile into the bake atlas of its grid mesh
// (box_gpu_terrain_bake): its texels are the fragments of the tile drawn top down into its cell,
// which reads the heights of every tile of its terrain that draws through the terrain index
// (box_render_terrain_index_t) for the ambient occlusion and the light of the sun. The includer
// includes mesh_common.wgsl first.
#pragma once

// A bake draw (box_gpu_terrain_bake_params_t): cell the pixel of the first texel of the cell in
// the atlas, the texels along its edge and the morph, 0 for the tile and 1 for the next coarser
// level; tile the corner of the tile in tiles of level 0, its edge in tiles of level 0 and the
// grid steps its edge vertices move out; sun the direction toward the sun, w 1 with one; grid the
// slot of the tile, the floats per row of the heights of a slot, the texels per row and the
// vertices along an edge, as p[1] of the surface of a tile; info.x the word offset of the
// terrain index.
struct TerrainBakeParams {
    cell: vec4f,
    tile: vec4f,
    sun: vec4f,
    grid: vec4f,
    info: vec4u,
};

@group(3) @binding(30) var<uniform> terrain_bake_params: TerrainBakeParams;
@group(3) @binding(31) var<storage, read> terrain_index: array<u32>;

// The surface assets of the grid mesh, the heights of every slot, for the fragment stage
@group(3) @binding(32) var<storage, read> terrain_bake_heights: array<vec4f>;

// Height of a texel of the heights of a slot, or of its rough heights, clamped to the image.
fn terrain_bake_texel(slot: u32, x: i32, z: i32, rough: bool) -> f32 {
    let w = i32(terrain_bake_params.grid.z);
    let r = (w + 1) / 2;
    let row = u32(terrain_bake_params.grid.y);
    let size = select(w, r, rough);
    let cx = u32(clamp(x, 0, size - 1));
    let cz = u32(clamp(z, 0, size - 1)) + select(0u, u32(w), rough);
    let i = slot * row * u32(w + r) + cz * row + cx;
    return terrain_bake_heights[i >> 2u][i & 3u];
}

// Height of the tile of a slot at a position on its grid in vertex steps, bilinear.
fn terrain_bake_height(slot: u32, g: vec2f) -> f32 {
    let t = g + vec2f(TERRAIN_BORDER);
    let f = floor(t);
    let a = t - f;
    let x = i32(f.x);
    let z = i32(f.y);
    let top = mix(terrain_bake_texel(slot, x, z, false), terrain_bake_texel(slot, x + 1, z, false),
        a.x);
    let bottom = mix(terrain_bake_texel(slot, x, z + 1, false),
        terrain_bake_texel(slot, x + 1, z + 1, false), a.x);
    return mix(top, bottom, a.y);
}

// Vertex steps along an edge of a tile.
fn terrain_bake_steps() -> f32 {
    return terrain_bake_params.grid.w - 1.0;
}

// Directions of the horizons of the ambient occlusion
const TERRAIN_AO_DIRECTIONS: u32 = 8u;

// Meters within which the heights occlude a texel
const TERRAIN_AO_REACH: f32 = 160.0;

// Steps of the march toward the sun and the growth of each step
const TERRAIN_SUN_STEPS: u32 = 32u;
const TERRAIN_SUN_GROWTH: f32 = 1.3;

// Tangent of the half angle of the penumbra of the heights
const TERRAIN_SUN_SOFTNESS: f32 = 0.03;

// Edge of a tile of level 0 in meters, from the terrain index.
fn terrain_index_size() -> f32 {
    return bitcast<f32>(terrain_index[terrain_bake_params.info.x + 1u]);
}

// Edge of the tile of a bake in meters.
fn terrain_bake_meters() -> f32 {
    return terrain_index_size() * terrain_bake_params.tile.z;
}

// Height of the terrain at a point in tiles of level 0, from the finest tile at or above a
// level that draws there: x the height, y 1 where a tile was found, else 0.
fn terrain_index_height(p: vec2f, level: u32) -> vec2f {
    let base = terrain_bake_params.info.x;
    let levels = terrain_index[base];
    let steps = terrain_bake_steps();
    for (var l = level; l < levels; l++) {
        let h = base + 4u + 4u * l;
        let first = vec2i(bitcast<i32>(terrain_index[h]), bitcast<i32>(terrain_index[h + 1u]));
        let dims = terrain_index[h + 2u];
        let size = vec2i(i32(dims & 0xFFFFu), i32(dims >> 16u));
        let q = p / exp2(f32(l));
        let c = vec2i(floor(q));
        let r = c - first;
        if (any(r < vec2i(0)) || any(r >= size)) {
            continue;
        }
        let e = terrain_index[base + terrain_index[h + 3u] + u32(r.y * size.x + r.x)];
        if (e == 0u) {
            continue;
        }
        return vec2f(terrain_bake_height(e - 1u, (q - vec2f(c)) * steps), 1.0);
    }
    return vec2f(0.0);
}

// Catmull-Rom weights of the texels at -1, 0, 1 and 2 around a fraction t.
fn terrain_cubic(t: f32) -> vec4f {
    let t2 = t * t;
    let t3 = t2 * t;
    return vec4f(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0,
        -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}

// Height of the tile of a bake at a position on its grid in vertex steps, Catmull-Rom between
// its heights, or between its rough heights for the next coarser level.
fn terrain_smooth(g: vec2f, rough: bool) -> f32 {
    let slot = u32(terrain_bake_params.grid.x);
    var t = g + vec2f(TERRAIN_BORDER);
    if (rough) {
        t *= 0.5;
    }
    let f = floor(t);
    let wx = terrain_cubic(t.x - f.x);
    let wz = terrain_cubic(t.y - f.y);
    var sum = 0.0;
    for (var j = 0; j < 4; j++) {
        var row = 0.0;
        for (var i = 0; i < 4; i++) {
            let x = i32(f.x) + i - 1;
            let z = i32(f.y) + j - 1;
            row += wx[i] * terrain_bake_texel(slot, x, z, rough);
        }
        sum += wz[j] * row;
    }
    return sum;
}

// Normal of the tile of a bake at a position on its grid, from the differences of its
// Catmull-Rom heights a quarter of a vertex step apart, or a half of one on the rough heights.
fn terrain_smooth_normal(g: vec2f, rough: bool) -> vec3f {
    let e = select(0.25, 0.5, rough);
    let step = terrain_bake_meters() / terrain_bake_steps();
    let dx = terrain_smooth(g + vec2f(e, 0.0), rough) - terrain_smooth(g - vec2f(e, 0.0), rough);
    let dz = terrain_smooth(g + vec2f(0.0, e), rough) - terrain_smooth(g - vec2f(0.0, e), rough);
    let scale = 1.0 / (2.0 * e * step);
    return normalize(vec3f(-dx * scale, 1.0, -dz * scale));
}

// Level of the tiles fine enough for the heights a distance away: the coarsest whose vertex
// steps are at most half of it.
fn terrain_index_level(meters: f32) -> u32 {
    let step = terrain_index_size() / terrain_bake_steps();
    return u32(max(floor(log2(max(meters / (2.0 * step), 1.0))), 0.0));
}

// Ambient occlusion of the heights at a point of the terrain in tiles of level 0 and its height:
// one minus the mean squared sine of the horizon of each direction, the share of the cosine
// weighted sky of a level surface the heights hide, from a texel of the tile to
// TERRAIN_AO_REACH.
fn terrain_bake_ao(p: vec2f, h: f32, texel: f32) -> f32 {
    let size = terrain_index_size();
    var hidden = 0.0;
    for (var d = 0u; d < TERRAIN_AO_DIRECTIONS; d++) {
        let angle = (f32(d) + 0.5) * (2.0 * PI / f32(TERRAIN_AO_DIRECTIONS));
        let dir = vec2f(cos(angle), sin(angle));
        var horizon = 0.0;
        var r = max(texel, 0.25);
        for (var s = 0u; s < 12u && r <= TERRAIN_AO_REACH; s++) {
            let q = terrain_index_height(p + dir * (r / size), terrain_index_level(r));
            if (q.y > 0.0) {
                let rise = (q.x - h) / r;
                horizon = max(horizon, rise * inverseSqrt(1.0 + rise * rise));
            }
            r *= 2.0;
        }
        hidden += horizon * horizon;
    }
    return 1.0 - hidden / f32(TERRAIN_AO_DIRECTIONS);
}

// Light of the sun the heights let through at a point of the terrain in tiles of level 0 and its
// height: the march toward the sun over the heights, soft by the distance to the heights that
// shadow it; 1 without a sun or with the sun straight up.
fn terrain_bake_sun(p: vec2f, h: f32, texel: f32) -> f32 {
    let sun = terrain_bake_params.sun;
    let across = length(sun.xz);
    if (sun.w == 0.0 || across < 1e-4) {
        return 1.0;
    }
    let size = terrain_index_size();
    let dir = sun.xz / across;
    let rise = sun.y / across;
    let lift = h + 0.5 * texel;
    var light = 1.0;
    var t = max(2.0 * texel, 1.0);
    for (var s = 0u; s < TERRAIN_SUN_STEPS && light > 0.0; s++) {
        let q = terrain_index_height(p + dir * (t / size), terrain_index_level(t));
        if (q.y > 0.0) {
            let over = lift + rise * t - q.x;
            light = min(light, clamp(0.5 + over / (t * TERRAIN_SUN_SOFTNESS * 2.0), 0.0, 1.0));
        }
        t *= TERRAIN_SUN_GROWTH;
    }
    return light;
}
