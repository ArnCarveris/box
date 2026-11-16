# Texture

Texture entities: images loaded from files (`Path`), generated tileable maps (`Gen`) and GPU
node graphs (`Graph`). Module `box.texture` (`BoxTextureImport`), header
`include/box/texture.h`, sources `src/texture/`. Materials sample a texture entity through
`PbrTextures` / `texture.Blend` (box.material), 2D shapes through `Image`, clouds and cookies
through their `texture` member.

Script names: `TexturePath` = `Path`, `TextureGen` = `Gen`, `TextureGraph` = `Graph`,
`TextureStatus` = `box.texture.Status`; Gen types in `box.texture.gen` (`gen.Layer`, ...); graph
settings in `box.texture.graph` (`graph.Cache`, `graph.Stats`, `graph.Preview`); node kinds in
`box.texture.nodes` (`nodes.Noise`, ...). Enum constants keep their full names (`GenNoise`,
`DomainSphere`, `GraphLinear`, `ChannelG`, `CellsEdge`, `NoiseBillow`, `AddressClamp`).

## Sources (one per entity)

| Component | Fields (defaults) | Notes |
|---|---|---|
| `Path` | `path`, `width`/`height` 0 (source size), `linear` false, `single_channel` false | stb_image raster or resvg `.svg` (one dimension keeps aspect). `linear` for linear-light color images (encoded to sRGB on load); leave off for ORM/normal maps. `single_channel` = R8 with mips, a quarter of RGBA8 (masks, cookies) |
| `Gen` | `width` 0 = 512, `height` 0 = width, `seed`, `layers`, `source`, `output` GenHeight/GenAlbedo/GenNormal, `colors` (ramp keys `{pos, {r,g,b}}`), `palette`, `palette_layer` 0, `strength` 1 | Seamless, deterministic CPU generator on the job system |
| `Graph` | `output` (node), `width` 0 = 512, `height` 0 = width, `domain` DomainPlane/DomainSphere, `format` GraphColor/GraphLinear/GraphSingle/GraphHdr, `rate` 0, `interpolate` false | Per texel evaluation of a node graph on the render backend |

The texture entity of a `Surface` (`Surface.texture`, box/surface.h) is also a texture entity:
materials sample its image of the same frame.

Engine written: `Texture` {`width`, `height`} on success, and `Status` {`state`
TextureLoading/TextureReady/TextureFailed, `revision` (successful loads), `error`}.

## Gen layers (`gen.Layer`)

`kind` GenNoise/GenBlocks/GenCells, `op` GenAdd/GenMultiply/GenMax, `amount` (none set = 1, may be
negative), `id_amount`, `seed`, `tones`, `tone_order` GenToneRandom/Checker/Courses/Columns,
`tone_amount`.
- GenNoise: `frequency` 4 (whole cells across), `octaves` 1, `lacunarity` 2, `gain` 0.5.
- GenBlocks: `courses` 8, `per_course` 4, `gap`, `jitter`, `stagger`, `softness` (1.3 x gap).
- GenCells: `count` 64, `squash` 1, `edge` 0.25, `distance` GenNearest/GenEdge.

The layers sum to a height field scaled to 0..1. A Gen with `source` derives from the source's
height field at its size: GenAlbedo through `colors`/`palette`, GenNormal by central differences.

```flecs
using box.texture

cobble_height { Gen: {width: 512, height: 512, seed: 111, layers: [
  {kind: GenCells, count: 100, distance: GenEdge, edge: 0.14, amount: 0.5, id_amount: 0.15}
  {kind: GenCells, count: 100, distance: GenEdge, edge: 0.45, amount: 0.6}
  {kind: GenNoise, frequency: 32, octaves: 3, amount: 0.15}
]} }
cobble { Gen: {source: cobble_height, output: GenAlbedo,
  colors: [{0, {72, 66, 58}}, {0.3, {120, 114, 106}}, {1, {222, 214, 202}}]} }
cobble_n { Gen: {source: cobble_height, output: GenNormal, strength: 3.6} }
hull { Path: {path: "etc/scenes/space_station/assets/hull.jpg"} }
mask { Path: {path: "test/assets/materials/fan_blades_normal.svg", width: 256, height: 256} }
```

## Graph nodes (`nodes.*`, one node component per child entity)

Each input is an entity member (`a`, `input`, ...) or, when 0, its constant (`a_value`,
`input_value`, ...). Values are Scalar or Vector (vec4); math nodes broadcast Scalars.

| Group | Nodes |
|---|---|
| Sources | `Constant {value}`, `Coords {scale}` (uv,0,1 on plane; xyz dir on sphere), `Time {scale, offset}` (animates), `Splat {in}` |
| Math | `Add`, `Subtract`, `Multiply`, `Divide` (0 where b = 0), `Min`, `Max`, `Power`, `Dot`, `Cross`, `Mix {a, b, factor}`, `Clamp {input, min, max}`, `Step {input, edge}`, `Smoothstep {input, edge0, edge1}`, `Abs`, `Negate`, `Floor`, `Fract`, `Sin`, `Cos`, `Length`, `Normalize`, `Remap {input, from_min, from_max, to_min, to_max, clamp}` |
| Channels | `Split {input, channel}` (Scalar), `Combine {r, g, b, a}` |
| Coordinates | `Warp {coords, offset, amount}` (offset centered at 0.5), `Transform {coords, angle (deg), axis, translate, scale}` |
| Patterns | `Noise {coords, evolve, frequency 4, octaves 1, lacunarity 2, gain 0.5, seed, fractal NoiseFbm/NoiseRidged/NoiseBillow, offset 1, channels 1/3/4}`, `Cells {distance CellsNearest/CellsSecond/CellsEdge, count 64, jitter 1, seed, edge 0.25, coords, evolve}` -> (dist, id, rand, rand), `Blocks {courses 8, per_course 4, gap, jitter, stagger, softness, tones, tone_order, seed, layer_seed}` -> (mask, id, tone, rand), plane only |
| Lookup | `Ramp {input, keys}` (max 32 keys, linear color out), `Curve {input, curve}` (box/curve.h, max 32 keys), `Image {texture, coords, address, linear}`, `Lut {texture, u, v, linear}` |
| Neighbourhood | `Blur {input, radius}`, `Distance {input, range}` (signed distance to 0.5 contour), `Normal {height, strength}` (for GraphLinear), `Slope {height, strength}`, `Gradient {height, strength}` (uphill vector in the space of `Coords`), `Flow {input, field, length, reach}`, `Transmittance {density, light, absorption, range, steps}` |

Radii and ranges are in texels of a 512 wide image, so results do not change with resolution.

```flecs
using box.texture

glow {
  uv     { nodes.Coords: {} }
  red    { nodes.Split: {input: uv, channel: ChannelR} }
  wave   { nodes.Sin: {input: red} }
  level  { nodes.Remap: {input: wave, from_min: -1, from_max: 1, to_min: 0.2, to_max: 0.8,
                         clamp: true} }
  tinted { nodes.Multiply: {a: level, b_value: {1, 0.5, 0.25, 1}} }
  Graph: {output: tinted, width: 512}
}
clouds {
  t     { nodes.Time: {scale: 0.04} }
  noise { nodes.Noise: {frequency: 4, octaves: 6, fractal: NoiseBillow, seed: 3, evolve: t} }
  Graph: {output: noise, width: 1024, format: GraphSingle, rate: 12}
}
```

Wait for a graph or load in script: `const ready: bool = tex[box.texture.Status].revision > 0`.

## Behavior rules

- `Path`, `Gen` and `Graph` on one entity is an error.
- Changing a member reloads/regenerates/rebuilds; the previous image stays in use until the new
  one is ready. `Path` files reload on FileWatch changes.
- Largest dimension is capped by `Surface.texture_quality` (default 4096; Low 512, Medium 1024,
  High 2048); changing it reloads textures.
- A layer that samples a texture has no effect until it is loaded. `single_channel` / R8
  textures sample as (r, 0, 0, 1).
- Gen: a `source` that has no `Gen` by the next frame fails it; `width`/`height` are ignored
  with a source. `palette_layer` must exist and not be GenNoise.
- Graph fails (`Status.error`) on a cycle, a deleted node, a child without a node component by
  the next frame, an input of the wrong type, `Blocks` on DomainSphere, more than 32 Ramp/Curve
  keys or an invalid curve.
- Graph tiling: DomainPlane tiles when noise frequencies and Transform scales are whole numbers
  and rotations are multiples of 90 degrees. DomainSphere outputs an equirectangular image
  (use width = 2 x height) and is seamless.
- DomainSphere `longitude_start/end`, `latitude_start/end` (radians, equal = whole) make the
  image a full resolution crop of the whole sphere graph that lines up on an `EllipsoidShell` of
  the same ranges; node radii keep their angular units, reads past the range clamp to its edge.
- `Image` nodes wait (TextureLoading) for their texture, evaluate after the graph they sample
  and again when its image changes; rebinding to another texture compiles nothing.
- Animated graphs (a `Time` with scale != 0) update `rate` times per second (0 = every frame)
  and skip updates while no draw sampled the image in the previous frame. A paused world clock
  shows a fixed image.
- `interpolate` crossfades two key images (doubles memory for the graph); ignored for
  GraphSingle and static graphs.

## Cost

- `Path` and `Gen` run on the job system (worker threads); `Graph` on the GPU.
- Graph values are cached by content hash in a shared cache (`graph.Cache {budget}`, 0 = 256 MB,
  LRU of unused values): an edit re-evaluates only stages after the edited node; identical
  subgraphs are shared across graphs.
- Neighbourhood nodes (`Blur`, `Distance`, `Normal`, `Slope`, `Gradient`, `Flow`, `Transmittance`) materialize their inputs into
  intermediate textures; `Distance` runs `BOX_TEX_DISTANCE_PASSES` (12) passes.
- `graph.Stats` (singleton) reports graphs, programs, dispatches, texels, `gpu_ms`, pool and key
  image bytes.

## C API and tools

- `box_texture_node_register(world, &box_texture_node_desc_t)`: add a custom node kind (WGSL
  function, inputs, params, flags `BOX_TEXTURE_NODE_*`); the built in kinds use it. Each kind's
  description is set as `graph.NodeKind` on its component entity.
- `graph.Preview {size 128, rate 4}` on a node writes `graph.PreviewStatus` (base64 PNG).
- `tools/texgraph/index.html`: browser graph editor that connects to a running app and uses
  previews. `tools/noise_texture.py`: writes a seamless fbm noise PNG.
