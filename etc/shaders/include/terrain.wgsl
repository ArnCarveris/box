// Tiles of a Terrain (box.terrain): a grid mesh of the unit square in XZ with skirts (vertices at
// y -1) displaced by the heights of the tile, which the backend copies into the surface assets
// of the grid mesh (box_gpu_terrain_heights). The surface of a tile (box_terrain_tile_surface):
// p[1] its slot, the floats per row of its heights, the texels per row and the vertices per
// edge; p[2] the focus relative to its center and the distances where its morph into the grid
// of the next coarser level starts and ends; p[4] its size, the skirt depth, its level and 1
// when bounded; p[5] the extent of a bounded terrain relative to its center. The includer
// declares surface_assets.
#pragma once

// Surface kind of a terrain tile (BOX_TERRAIN_SURFACE)
const TERRAIN_SURFACE: f32 = 16.0;

// Texels of the heights beyond each edge of a tile (BOX_TERRAIN_BORDER)
const TERRAIN_BORDER: f32 = 2.0;

struct TerrainVertex {
    position: vec3f,
    normal: vec3f,
};

// Height of a texel of the heights of the tile of surface_data, clamped to the image.
fn terrain_texel(x: i32, z: i32) -> f32 {
    let w = i32(surface_data.p[1].z);
    let row = u32(surface_data.p[1].y);
    let cx = u32(clamp(x, 0, w - 1));
    let cz = u32(clamp(z, 0, w - 1));
    let i = u32(surface_data.p[1].x) * row * u32(w) + cz * row + cx;
    return surface_assets[i >> 2u][i & 3u];
}

// Height at a position on the grid of the tile in vertex steps from its first vertex, bilinear
// between the texels.
fn terrain_height(g: vec2f) -> f32 {
    let t = g + vec2f(TERRAIN_BORDER);
    let f = floor(t);
    let a = t - f;
    let x = i32(f.x);
    let z = i32(f.y);
    let top = mix(terrain_texel(x, z), terrain_texel(x + 1, z), a.x);
    let bottom = mix(terrain_texel(x, z + 1), terrain_texel(x + 1, z + 1), a.x);
    return mix(top, bottom, a.y);
}

// Position and normal in the unit frame of the tile of a vertex of its grid mesh. The odd
// vertices slide onto their even neighbours by the morph factor of their distance to the focus
// (CDLOD), so at the end of its range the tile has the grid and the heights of the coarser level
// and meets its tiles without cracks. The normal is of central differences one vertex step
// apart, two at a full morph as on the coarser level, scaled into the unit frame.
fn terrain_vertex(position: vec3f) -> TerrainVertex {
    let n = surface_data.p[1].w - 1.0;
    let size = surface_data.p[4].x;
    let g = round((position.xz + vec2f(0.5)) * n);
    let offset = (g / n - vec2f(0.5)) * size;
    let d = length(offset - surface_data.p[2].xy);
    let span = max(surface_data.p[2].w - surface_data.p[2].z, 1e-4);
    let k = clamp((d - surface_data.p[2].z) / span, 0.0, 1.0);
    var m = g - fract(g * 0.5) * 2.0 * k;
    if (surface_data.p[4].w != 0.0) {
        let lo = (surface_data.p[5].xy / size + vec2f(0.5)) * n;
        let hi = (surface_data.p[5].zw / size + vec2f(0.5)) * n;
        m = clamp(m, lo, hi);
    }

    let h = terrain_height(m);
    let reach = 1.0 + k;
    let dx = terrain_height(m + vec2f(reach, 0.0)) - terrain_height(m - vec2f(reach, 0.0));
    let dz = terrain_height(m + vec2f(0.0, reach)) - terrain_height(m - vec2f(0.0, reach));
    let scale = n / (2.0 * reach);
    var y = h;
    if (position.y < -0.5) {
        y = h - surface_data.p[4].y;
    }

    let unit = m / n - vec2f(0.5);
    return TerrainVertex(vec3f(unit.x, y, unit.y),
        normalize(vec3f(-dx * scale, 1.0, -dz * scale)));
}
