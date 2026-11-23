// Tiles of a Terrain (box.terrain): a grid mesh of the unit square in XZ with skirts (vertices at
// y -1) displaced by the heights of the tile and its rough heights, which the backend copies
// into the surface assets of the grid mesh (box_gpu_terrain_heights). The surface of a tile
// (box_terrain_tile_surface) holds its TerrainTileParams in p[1] to p[5]. The terrain shader
// (terrain.wgsl) reads them from the surface stream, the other stages that place the tiles from
// surface_data (terrain_vertex). The includer declares surface_assets.
#pragma once

// Surface kind of a terrain tile (BOX_TERRAIN_SURFACE)
const TERRAIN_SURFACE: f32 = 16.0;

// Texels of the heights beyond each edge of a tile (BOX_TERRAIN_BORDER)
const TERRAIN_BORDER: f32 = 2.0;

struct TerrainVertex {
    position: vec3f,
    normal: vec3f,
};

// Morph factor a bake forces on the vertices of its tile (vs_terrain_bake), below 0 for the
// morph of the distance to the focus.
var<private> terrain_forced_morph: f32 = -1.0;

// Grid steps a bake moves the vertices on the edges of its tile outward, so the texels on the
// edges of its cell are inside the triangles (vs_terrain_bake).
var<private> terrain_pad: f32 = 0.0;

// Grid position of the vertex terrain_vertex placed last, in vertex steps from the first vertex.
var<private> terrain_grid: vec2f = vec2f(0.0);

// The values of the surface of a tile its vertices read, p[1] to p[5] of its SurfaceData: grid
// its slot, the floats per row of its heights, the texels per row and the vertices per edge;
// morph the focus relative to its center and the distances where its morph starts and ends;
// masks x 1 when its masks weigh the layers of a TextureBlend; shape its size, the skirt depth,
// its level and 1 when bounded; clip the extent of a bounded terrain relative to its center.
struct TerrainTileParams {
    grid: vec4f,
    morph: vec4f,
    masks: vec4f,
    shape: vec4f,
    clip: vec4f,
};

// The values of the tile of surface_data.
fn terrain_params() -> TerrainTileParams {
    return TerrainTileParams(surface_data.p[1], surface_data.p[2], surface_data.p[3],
        surface_data.p[4], surface_data.p[5]);
}

// Height of a texel of the heights of the tile of a slot of the surface assets, clamped to the
// image. The slot holds the rows of the heights and after them the rows of the rough heights, the
// heights of the next coarser level at every second texel; every slot of the mesh has the rows
// and texels of grid (TerrainTileParams.grid).
fn terrain_grid_texel(grid: vec4f, slot: u32, x: i32, z: i32) -> f32 {
    let w = i32(grid.z);
    let row = u32(grid.y);
    let cx = u32(clamp(x, 0, w - 1));
    let cz = u32(clamp(z, 0, w - 1));
    let i = slot * row * u32(w + (w + 1) / 2) + cz * row + cx;
    return surface_assets[i >> 2u][i & 3u];
}

// Height of a texel of the rough heights of the tile of a slot, clamped to the image.
fn terrain_grid_rough_texel(grid: vec4f, slot: u32, x: i32, z: i32) -> f32 {
    let w = i32(grid.z);
    let r = (w + 1) / 2;
    let row = u32(grid.y);
    let cx = u32(clamp(x, 0, r - 1));
    let cz = u32(clamp(z, 0, r - 1));
    let i = slot * row * u32(w + r) + (u32(w) + cz) * row + cx;
    return surface_assets[i >> 2u][i & 3u];
}

// Height at a position on the grid of a tile in vertex steps from its first vertex, bilinear
// between the texels.
fn terrain_height_of(grid: vec4f, g: vec2f) -> f32 {
    let slot = u32(grid.x);
    let t = g + vec2f(TERRAIN_BORDER);
    let f = floor(t);
    let a = t - f;
    let x = i32(f.x);
    let z = i32(f.y);
    let top = mix(terrain_grid_texel(grid, slot, x, z), terrain_grid_texel(grid, slot, x + 1, z),
        a.x);
    let bottom = mix(terrain_grid_texel(grid, slot, x, z + 1),
        terrain_grid_texel(grid, slot, x + 1, z + 1), a.x);
    return mix(top, bottom, a.y);
}

// Height of the next coarser level at a position on the grid of a tile in vertex steps from its
// first vertex, bilinear between the texels of the rough heights.
fn terrain_rough_of(grid: vec4f, g: vec2f) -> f32 {
    let slot = u32(grid.x);
    let t = (g + vec2f(TERRAIN_BORDER)) * 0.5;
    let f = floor(t);
    let a = t - f;
    let x = i32(f.x);
    let z = i32(f.y);
    let top = mix(terrain_grid_rough_texel(grid, slot, x, z),
        terrain_grid_rough_texel(grid, slot, x + 1, z), a.x);
    let bottom = mix(terrain_grid_rough_texel(grid, slot, x, z + 1),
        terrain_grid_rough_texel(grid, slot, x + 1, z + 1), a.x);
    return mix(top, bottom, a.y);
}

// Height at a position on the grid of a tile, from its heights toward those of the next coarser
// level by the morph factor.
fn terrain_morphed_of(grid: vec4f, g: vec2f, k: f32) -> f32 {
    return mix(terrain_height_of(grid, g), terrain_rough_of(grid, g), k);
}

// Morph factor of a vertex of a tile at grid position g (in vertex steps), from the distance of
// its offset from the center of the tile to the focus; terrain_forced_morph when a bake forces
// one.
fn terrain_morph_of(t: TerrainTileParams, g: vec2f) -> f32 {
    let n = t.grid.w - 1.0;
    let offset = (g / n - vec2f(0.5)) * t.shape.x;
    let d = length(offset - t.morph.xy);
    let span = max(t.morph.w - t.morph.z, 1e-4);
    var k = clamp((d - t.morph.z) / span, 0.0, 1.0);
    if (terrain_forced_morph >= 0.0) {
        k = terrain_forced_morph;
    }
    return k;
}

// Grid position of a vertex of a tile: g in vertex steps slid onto its even neighbours by the
// morph factor k (CDLOD), clamped to a bounded terrain and moved out by terrain_pad on the
// edges. Sets terrain_grid.
fn terrain_slide(t: TerrainTileParams, g: vec2f, k: f32) -> vec2f {
    let n = t.grid.w - 1.0;
    var m = g - fract(g * 0.5) * 2.0 * k;
    if (t.shape.w != 0.0) {
        let lo = (t.clip.xy / t.shape.x + vec2f(0.5)) * n;
        let hi = (t.clip.zw / t.shape.x + vec2f(0.5)) * n;
        m = clamp(m, lo, hi);
    }
    m += select(vec2f(0.0), vec2f(-terrain_pad), g <= vec2f(0.0)) +
        select(vec2f(0.0), vec2f(terrain_pad), g >= vec2f(n));
    terrain_grid = m;
    return m;
}

// Position and normal in the unit frame of a tile of a vertex of its grid mesh. The odd
// vertices slide onto their even neighbours by the morph factor of their distance to the focus
// (CDLOD) and the heights blend into the rough heights, so at the end of its range the tile has
// the grid and the heights of the coarser level and meets its tiles without cracks. The normal
// is of central differences one vertex step apart, two at a full morph as on the coarser level,
// scaled into the unit frame.
fn terrain_vertex_of(t: TerrainTileParams, position: vec3f) -> TerrainVertex {
    let n = t.grid.w - 1.0;
    let g = round((position.xz + vec2f(0.5)) * n);
    let k = terrain_morph_of(t, g);
    let m = terrain_slide(t, g, k);
    let h = terrain_morphed_of(t.grid, m, k);
    let reach = 1.0 + k;
    let dx = terrain_morphed_of(t.grid, m + vec2f(reach, 0.0), k) -
        terrain_morphed_of(t.grid, m - vec2f(reach, 0.0), k);
    let dz = terrain_morphed_of(t.grid, m + vec2f(0.0, reach), k) -
        terrain_morphed_of(t.grid, m - vec2f(0.0, reach), k);
    let scale = n / (2.0 * reach);
    var y = h;
    if (position.y < -0.5) {
        y = h - t.shape.y;
    }

    let unit = m / n - vec2f(0.5);
    return TerrainVertex(vec3f(unit.x, y, unit.y),
        normalize(vec3f(-dx * scale, 1.0, -dz * scale)));
}

// Position of a vertex of a tile as terrain_vertex_of places it, without its normal.
fn terrain_position_of(t: TerrainTileParams, position: vec3f) -> vec3f {
    let n = t.grid.w - 1.0;
    let g = round((position.xz + vec2f(0.5)) * n);
    let k = terrain_morph_of(t, g);
    let m = terrain_slide(t, g, k);
    var y = terrain_morphed_of(t.grid, m, k);
    if (position.y < -0.5) {
        y -= t.shape.y;
    }

    let unit = m / n - vec2f(0.5);
    return vec3f(unit.x, y, unit.y);
}

// Position and normal of a vertex of the tile of surface_data (terrain_vertex_of).
fn terrain_vertex(position: vec3f) -> TerrainVertex {
    return terrain_vertex_of(terrain_params(), position);
}
