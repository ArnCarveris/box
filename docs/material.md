# Material

Surface appearance of shapes: color, opacity, tint, PBR parameters, extension layers (clear
coat, sheen, anisotropy, transmission, water, ...), texture layers and UV mapping. Module
`box.material` (`BoxMaterialImport`), header `include/box/material.h`, source
`src/material/material.c`. UV/texture components live in the child module
`box.material.texture` with the `Texture` prefix dropped: write `texture.Transform`,
`texture.Mapping`, `texture.Scroll`, `texture.Blend`, `texture.Layer`, `texture.Layers`,
`texture.Wrap`, `texture.Frame` in script.

## Base components

| Component | Fields (defaults) | Notes |
|---|---|---|
| `Rgb` | `r, g, b` (display encoded bytes) | Base color. Without Rgb a shape is {204,204,204} |
| `Opacity` | `value` 1 | Multiplies into every shape below the entity (no shape needed on it). < 1 = sorted blended pass, no shadow. In a UI tree it composites the subtree instead |
| `AlphaCutoff` | `value` 0.5 | Alpha test (opacity x albedo alpha). Opaque/no BlendMode: drawn in the opaque pass with holes in depth and shadows. Needs `Scene.alpha_cutout` |
| `Tint` | `r, g, b, a` (a = strength, 0 none, 255 replace) | Mixes base color before lighting; composes down ChildOf/Parent. Not emission; bakes ignore it |
| `PbrMaterial` | `metallic` 0, `roughness` 1 | Unset fields keep the default: a PbrMaterial with only metallic is fully rough |
| `Emissive` | `strength` 0, `color` | Not tinted |
| `BlendMode` (enum) | `Opaque`, `AlphaBlend`, `PremultipliedAlpha`, `Additive`, `Multiply`, `Screen`, `Subtractive`, `Min`, `Max` | Needs `Scene.blend_modes` |
| `BackFaceMode` (enum) | `BackFaceCull` (default), `BackFacePlain` | Plain: back faces show material constants, no textures |

## Extension layers (3D meshes only; 2D shapes and text ignore them)

| Component | Fields (defaults) | Scene flag |
|---|---|---|
| `ClearCoat` | `strength` 1, `roughness` 0 | `clear_coat` |
| `SpecularAnisotropy` | `strength` 1, `rotation` 0 (rad from +U) | `anisotropy` |
| `Sheen` | `color` white, `roughness` 0.5 | `sheen` |
| `DiffuseTransmission` | `factor` 0.5, `color`, `thickness` 0, `blur` 0 | `diffuse_transmission` |
| `Fresnel` | `color` white, `strength` 1, `power` 5, `bias` 0, `opacity` 0 | `fresnel` |
| `Transmission` | `transmission_factor` 0, `ior` 1.5, `thickness_factor` 0, `attenuation_distance` 1e10, `attenuation_color` white | `transmission` |
| `Distortion` | `strength` 0.004 (fraction of image height), `scale` 1, `speed` 0.5 | `distortion` |
| `Water` | `shallow_color` {60,170,160}, `deep_color` {8,40,64}, `depth` 3, `visibility` 2, `refraction` 0.02, `foam_color` {240,245,245}, `foam_width` 0.3, `foam_texture` 0, `foam_scale` 1, `ripple_scale` 0, `ripple_velocity` | `water` |
| `WindSway` | `bend` 0 (world units per unit of wind speed at full height), `height` 1, `base` 0 (world Y of no motion), `flutter` 0 (world units at full wind), `flutter_frequency` 2 Hz, `flutter_reach` 0.3 | `wind` |

Defaults above are what a component added in script gets (constructors). A C literal zeroes
unset fields.

## Textures and UVs

| Component | Fields (defaults) | Notes |
|---|---|---|
| `PbrTextures` | `albedo`, `emissive`, `orm`, `normal` (texture entities, 0 none) | Albedo display encoded, multiplies Rgb and opacity. ORM stored: R occlusion, G x roughness, B x metallic. Normal: tangent space, green up |
| `texture.Transform` | `scale_x` 1, `scale_y` 1, `offset_x` 0, `offset_y` 0 | UV scale then offset; UV 0,0 = top left |
| `texture.Wrap` (enum) | `Repeat` (default), `Clamp`, `Mirror` | Sampler addressing |
| `texture.Mapping` | `mode` MappingUv / MappingObject / MappingWorld / MappingFrame, `size` 1, `blend` 0 | Planar projection on the dominant normal axis; `blend` > 0 = triplanar (needs `Scene.triplanar`) |
| `texture.Frame` (tag) | | Frame for MappingFrame (and the scale reference of MappingObject) for its subtree |
| `texture.Scroll` | `velocity`, `normal_velocity` (texture widths/s) | Endless UV scroll; needs `Scene.texture_scroll` |
| `texture.Blend` | `mask`, `layers[4]` (entities with PbrTextures + optional texture.Transform), `heights[4]`, `contrast` 0 | Mask RGBA weighs layers 0..3; needs `Scene.texture_blend`. Shape's PbrTextures ignored except emissive |
| `texture.Layers` | `layers[4]` (entities with `texture.Layer`, 0 unused) | Applied in order over the resolved surface (after PbrTextures, texture.Blend or TileTerrain, before lighting); needs `Scene.texture_layers`. Part of the batch like PbrTextures |
| `texture.Layer` | `amount` 1, `direction` {0,1,0}, `facing` 0, `mask` 0, `cavity` 0, `contrast` 8, `albedo` LayerMix, `roughness` LayerMix, `metallic` LayerKeep, `occlusion` LayerKeep, `normal` 0, `porosity` 0 | On a layer entity, with its own Rgb, PbrMaterial, PbrTextures, texture.Mapping (default world), texture.Transform, texture.Scroll |

## Texture layers

A shape opts in to `texture.Layers`; each layer entity paints over or modifies the surface
beneath it, which already includes the shape's own Rgb, PbrMaterial, PbrTextures, texture.Blend or
TileTerrain. Weather (snow, wet, puddles) is a set of layer entities a scene animates.

- Appearance: the layer's `Rgb` and `PbrMaterial` constants (white, roughness 1, metallic 0
  without them) times its `PbrTextures` (albedo; ORM green roughness, blue metallic, red
  occlusion; normal). Any texture entity works, a `texture.Graph` output too, so procedural and
  animated layers work. Without textures a layer is a flat color/roughness.
- Placement: `texture.Mapping` of the layer entity; without one it is `MappingWorld` with size 1,
  so a layer runs on across shapes. `blend` > 0 blends the three projections. `MappingUv` uses
  the UVs of the mesh; `MappingObject` and `MappingFrame` project like `MappingWorld`.
- Coverage: a score from 0 to 1 per fragment: how well the shading normal faces `direction`
  (`facing` 0 ignores it, larger only allows normals closer to it, 1 only those facing it fully;
  `normal` mixes it toward the geometric normal, so a flattening water film fills the bumps),
  times the red of `mask` raised by `cavity` times how occluded or low the surface is (1 - ORM
  red, or 1 - the height of a `texture.Blend` with heights), so positive fills crevices and low
  spots first, negative ridges first, without holding back open surfaces. `amount` moves a threshold over the
  score: 0.2 snow is a dusting on the flattest tops, 1 covers everything the rules allow.
  `contrast` sharpens the edge.
- Change: `albedo`, `roughness`, `metallic`, `occlusion` each `LayerKeep`, `LayerMix` (replace by
  the layer value where covered) or `LayerMultiply` (multiply by it). `normal` 0..1 mixes the
  shading normal toward the layer normal (a layer without a normal map flattens, as a water film).
- `porosity` 0..1 scales the change of albedo and roughness by the porosity of the surface beneath,
  `(1 - metallic) * roughness`: rough soil, bark and stone change fully, glossy leaves little,
  metals not at all. The albedo darkens by a plain multiply weighted that way (a simplification of
  Lagarde's wet surfaces model).
- Alpha never changes; alpha tested shapes keep their holes. Transmission and Distortion shapes,
  2D shapes and text ignore layers. Probe and reflection captures see them.
- `Water` (in a scene with `Scene.water`) uses only the normals of its layers: they blend into
  its ripple normal by their coverage (`normal` times the coverage, the cavity ignored), before
  the waves. Their albedo, roughness, metallic and occlusion do nothing on Water. Rain on a
  brook is the puddle layer with a `Ripples` graph as its normal (texture.md).

```flecs
$ { Scene: {texture_layers: true, texture_blend: true, alpha_cutout: true} }

snow_noise { n { nodes.Noise: {frequency: 6, octaves: 4} }
  Graph: {output: n, width: 256, format: GraphSingle} }
puddle_noise { n { nodes.Noise: {frequency: 3, octaves: 3, seed: 7} }
  Graph: {output: n, width: 256, format: GraphSingle} }

snow {   // white rough cover on the tops
  texture.Layer: {amount: 0.3, facing: 0.6, mask: snow_noise}
  Rgb: {245, 248, 255}; PbrMaterial: {roughness: 0.9}
  texture.Mapping: {mode: MappingWorld, size: 2}
}
wet {    // no textures: darker and glossier everywhere, metals unchanged
  texture.Layer: {albedo: LayerMultiply, roughness: LayerMultiply, normal: 0.3, porosity: 1}
  Rgb: {153, 153, 153}; PbrMaterial: {roughness: 0.3}
}
puddles {
  texture.Layer: {amount: 0.5, mask: puddle_noise, cavity: 0.8, facing: 0.95,
    albedo: LayerMultiply, roughness: LayerMix, normal: 1}
  Rgb: {110, 110, 110}; PbrMaterial: {roughness: 0.02}
}

rock { Box: {1, 1, 1}; texture.Layers: {layers: [wet, snow]} }
brook { Quad: {4, 20}; Water: {}; texture.Layers: {layers: [puddles]} }
```

## Tags

| Tag | What it does |
|---|---|
| `NoShadow` | Entity and descendants cast no shadow |
| `LightFixture` | Shape skips shadow tiles of lights inside its world bounds (bulbs, lantern glass) |
| `DynamicMaterial` | Material changes every frame; applies to descendants (private record) |
| `MaterialId` (component, engine written) | Slot of the material table, read only |

## Script

```flecs
using box.geometry
using box.material
using box.texture
using box.atmosphere

assets {
  hull_a { Path: {path: "etc/scenes/space_station/assets/hull.jpg"} }
  hull_n { Path: {path: "etc/scenes/space_station/assets/hull_normal.jpg"} }
  hull_orm { Path: {path: "etc/scenes/space_station/assets/hull_orm.jpg"} }
  prefab Hull { Rgb: {226, 230, 236}; PbrMaterial: {metallic: 0.55, roughness: 0.62}
    PbrTextures: {albedo: hull_a, normal: hull_n, orm: hull_orm}
    texture.Mapping: {MappingObject, size: 3, blend: 0.25} }
  prefab Ice { Rgb: {210, 236, 250}; PbrMaterial: {roughness: 0.15}
    ClearCoat: {strength: 1, roughness: 0.05}
    Transmission: {transmission_factor: 0.6, ior: 1.31, thickness_factor: 0.25,
      attenuation_distance: 0.6, attenuation_color: {170, 214, 240}} }
  prefab Leaf { Rgb: {111, 145, 100}; BackFaceMode: {BackFacePlain}; PbrMaterial: {roughness: 0.85} }
}

$ { Scene: {transmission: true, clear_coat: true, triplanar: true, texture_blend: true,
  water: true, texture_scroll: true, alpha_cutout: true, wind: true}
  Wind: {direction: {1, 0}, speed: 5} }

ground { Box: {30, 0.2, 30}; Rgb: {255, 255, 255}; PbrMaterial: {roughness: 1}
  texture.Blend: {mask: patches, layers: [dirt_a, dirt_b]}
  texture.Mapping: {mode: MappingWorld, size: 8} }
river { Box: {10, 0.05, 40}; Water: {}; PbrTextures: {normal: water_n}
  texture.Scroll: {velocity: {0.05, 0.02}} }
prefab Grass { Quad: {0.3, 0.8}; PbrTextures: {albedo: blade}; AlphaCutoff: {}
  BackFaceMode: {BackFacePlain}; WindSway: {bend: 0.03, height: 0.8, flutter: 0.03} }
```

## Behavior rules

- `Rgb`, `PbrMaterial`, `Emissive`, `AlphaCutoff`, `Transmission`, `ClearCoat`,
  `SpecularAnisotropy`, `Sheen`, `DiffuseTransmission`, `Fresnel`, `Water`, `Distortion`,
  `WindSway`, `texture.Transform`, `texture.Mapping`, `texture.Scroll`, `PbrTextures`,
  `texture.Blend` and `texture.Layers` are inherited by prefab instances (OnInstantiate Inherit). Changing the prefab updates all
  instances that do not override.
- `Scene` features are all false by default. A shape needing a feature that is off draws as if
  it lacked the component (no error). Set them on the Scene root, or `$ { Scene: {...} }` for
  the default scene.
- Opacity < 1 (own times ancestors') or a non-Opaque BlendMode moves the shape to the sorted
  blended pass, where it casts no shadow.
- `Transmission`, `Distortion` and `Water` draw in the blended pass over a snapshot of what the
  view drew before (taken at most 4 times a frame), cast no shadow, are not captured, and
  ignore Opacity, BlendMode and AlphaCutoff. Water also ignores Transmission and texture.Blend,
  and Distortion is ignored on Water.
- Water is seen from above only: no underwater views, no back faces. Waves come from
  `PbrTextures.normal` scrolled by `texture.Scroll`.
- `Fresnel` here is an artistic emissive rim, separate from physical Fresnel.
- A texture layer is a no-op until loaded or when missing. Any layer samples a single channel
  texture as (r, 0, 0, 1).
- Parametric surfaces keep their own UVs under MappingObject/World, except a Sweep with
  MappingObject (lengths in path units / size) and MappingFrame.
- `WindSway` moves the vertices in the `Wind` (docs/atmosphere.md) in every pass: depth
  prepass, forward, shadows of every light, planar reflections, captures and the wireframe. The
  main bend depends on the world position only: `clamp((y - base) / height, 0, 1)^2 * bend *`
  the wind strength there, so the trunk, branches and leaf cards of a plant built as separate
  shapes with the same `bend`, `height` and `base` move together without cracks. Vertices that
  move sideways drop so branches do not stretch, and normals bend with the displacement.
- The flutter is a fast per vertex motion of `flutter` world units at full wind (speed 10),
  weighted by the distance of the vertex from the origin of its shape over `flutter_reach`: a leaf
  card with its origin at its stem stays attached there.
- Shapes with a `WindSway` draw in batches of their own with the wind variants of the pipelines;
  every other shape keeps the pipelines, bounds, culling and shadow caching it has without the
  wind. Their culling bounds grow by the largest displacement, so they do not pop.
- ClearCoat, SpecularAnisotropy, Sheen, DiffuseTransmission, Transmission and Distortion are
  transitionable (`Transition`); adding/removing animates from/to strength 0 with on_add /
  on_remove.

## Cost

- Material records are shared by content hash: shapes with identical values share one slot.
  Prefabs, `DynamicMaterial` batches, `texture.Scroll`, `WindSway`, moving `Distortion`/rippling
  `Water` and running transitions own a private slot packed every frame or on change.
- `WindSway` costs a material read and the sway math per vertex in every pass of its batches,
  and re-renders the shadow tiles and cascades of its casters every frame while the wind blows.
- Put materials on prefabs so instances share them; avoid `DynamicMaterial` unless values truly
  change every frame.
- `PbrTextures` and `texture.Blend` are part of the batch, not the material table: every distinct
  texture set is a separate batch.
- `texture.Layers` is part of the batch: shapes with the same layers share a batch drawn with
  the layers entry points of separate modules of the mesh shaders (compiled with
  `BOX_TEXTURE_LAYERS`, only in scenes with `Scene.texture_layers`) and their own bind group
  layouts. Shapes without it keep their shader modules, pipelines, bind groups, material records
  and batches unchanged, with `Scene.texture_layers` on or off; a Water batch with layers
  draws with the `fs_water_layers` entry point, Water without layers keeps its pipeline. The values of a layer live in one small uniform buffer per
  layer entity, written only when they change, so animating `amount` touches no shape and no
  batch. The maps of the layers (and their masks) are blitted into one texture array per layer
  set, built again when an image changes (every update of an animated graph).
- Each Scene feature compiles a larger shader; enable only what the scene uses.
