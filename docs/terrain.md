# Terrain

Flat tile terrain: a grid of tiles of up to 16 kinds in the XZ plane, drawn with hand painted
transition atlases where kinds meet. Module `box.terrain` (`BoxTerrainImport`), header
`include/box/terrain.h`, source `src/terrain/terrain.c`.

## Components

| Component | Fields | What it does |
|---|---|---|
| `TileTerrain` | `size` (tile edge, not positive/finite = 1), `kinds` (vec of `TileKind`, max `BOX_TERRAIN_KINDS` = 16), `tiles` (grid string) | Registered shape: a unit plane scaled to `columns * size` by `rows * size`, centered on the entity |
| `TileKind` | `symbol` (char), `textures` (entity with `PbrTextures` atlases, 0 = white) | One kind; script form `{'#', cobble}` |
| `TileTerrainGrid` (internal, `src/terrain/terrain.h`) | `texture`, `columns`, `rows` | Written by the engine: child texture entity holding an R8 `TexturePixels` image, one pixel per tile = kind index |

## Script

```flecs
using box.material
using box.terrain
using box.texture

grass_a { Path: {path: "etc/scenes/mossvale/textures/tiles/grass.png"} }
grass_n { Path: {path: "etc/scenes/mossvale/textures/tiles/grass_normal.jpg"} }
grass { PbrTextures: {albedo: grass_a, normal: grass_n} }
road { PbrTextures: {albedo: road_atlas, normal: road_atlas_n} }

$ { Scene: {shader_features: {Extended: {texture_blend: true}}} }

terrain {
  Position: {0, 0, 0}
  Rgb: {255, 255, 255}
  PbrMaterial: {roughness: 0.95}
  TileTerrain: {size: 2, kinds: [{'.', grass}, {'#', road}], tiles: `
    ...##...
    ...##...
    ..##....
  `}
}
```

Full example: `etc/scenes/mossvale/tiles.flecs` (10 kinds, 128 columns).

## Grid rules

- One line per row, rows run from -Z to +Z; symbols in a row run from -X to +X.
- Whitespace (space, tab, CR, VT, FF) is ignored, so indent freely. Empty lines are not rows.
- Rows shorter than the longest are padded with kind 0 (the first kind).
- An unknown symbol becomes kind 0 and logs `TileTerrain '<path>': unknown tile symbol 'x'`.
- More than 16 kinds logs an error; only the first 16 draw.
- Changing `TileTerrain` rewrites the grid image; removing it deletes the grid texture.

## Drawing and atlases

- Needs `texture_blend` in `Scene.shader_features.Extended` (the texture_blend variant of the mesh
  pipeline). Without it the shape draws as a plain plane with its own `PbrTextures`.
- `Rgb`, `PbrMaterial`, `Tint` and `Emissive` apply as with a `TextureBlend`.
- Kinds draw in list order, each over the ones before it, on the dual grid: the cell between
  four tile centers draws the lowest of their kinds whole, then each higher kind through the
  atlas variant matching which corners are that kind or higher.
- Each kind's albedo, ORM and normal are a 4x4 atlas of variants; variant `i` is at column
  `i % 4`, row `i / 4`. Corner bits: bit 0 top left (-x, -z), bit 1 top right, bit 2 bottom
  left, bit 3 bottom right. Variant 15 = whole tile, variant 0 unused. Albedo alpha covers the
  kinds below.
- An atlas twice as wide adds whole-tile variants 16..31, which half the whole cells pick at
  random.
- Atlases are scaled to the largest one of the terrain. Power of two cell sizes keep each mip
  inside its cell. A whole tile must tile with itself and with the edges of every variant.
- Atlas generators: `test/assets/textures/gen_tile_atlases.py`,
  `etc/scenes/mossvale/textures/terrain.py`.

## Cost

- All terrains share one 4-vertex unit plane mesh (the key is empty); the grid size is the
  instance scale. Per terrain: one R8 texture of `columns x rows` pixels.

## Tests

- `test/cpu/core` suite `TileTerrain`, `test/gpu/render` suite `TileTerrain`.
