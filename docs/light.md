# Light

Lights, ambient, shadows atlas, baked indirect light, reflection probes, planar reflections,
fog volumes, star fields and environment (IBL) sources. Module `box.light` (`BoxLightImport`),
header `include/box/light.h`, source `src/light/light.c`. Color comes from the entity's `Rgb`,
direction from its transform.

## Lights

| Component | Fields (defaults) | Notes |
|---|---|---|
| `DirectionalLight` | `intensity` 1, `bake` Mixed | Travels along local +Z. Intensity = irradiance on a facing surface: pi with white Rgb lights a surface at its base color |
| `PointLight` | `intensity` 1, `range` 10, `bake`, `shadows` false, `shadow_resolution` 512 | Falloff `intensity / d^2` windowed to 0 at `range`. Range and intensity scale with uniform world scale (`range * s`, `intensity * s^2`) |
| `SpotLight` | `intensity` 1, `range` 10, `inner_angle` 20, `outer_angle` 30 (half angles, deg), `bake`, `shadows`, `shadow_resolution` 512 | Along local +Z. Shadow tile covers up to 80 deg |
| `AmbientLight` | `sky` {118,130,149}, `ground` {79,74,68}, `intensity` 1 | Hemisphere ambient; on the Scene root (singleton for the default scene); multiple sum |
| `Cookie` (`LightCookie`) | `texture`, `projection` Plane/Light, `size` 4000, `height` 1500, `offset`, `velocity`, `threshold` 0.5, `softness` 0.5, `strength` 1 | Single channel texture scaling direct light of the first DirectionalLight only; needs `Scene.cookies` |

`bake` (`LightBakeMode`, script constants `Mixed`, `Realtime`, `Baked`): Mixed = realtime direct
+ baked bounce; Realtime = left out of the bake; Baked = only in probes (no realtime light,
no per-frame cost, no view shadows). Without a `Bake` in the scene every mode is realtime.

## Scene-level settings

| Component | Fields (defaults) | Notes |
|---|---|---|
| `ShadowAtlas` | `resolution` 2048 (256..8192), `min_tile` 64 | Atlas for spot/point light shadow tiles, max 64 tiles, nearest lights first |
| `Bake` (`LightBake`) | `probe_spacing` 2, `bounces` 2 (0..16), `capture_size` 16 (1..256), `budget_ms` 2, `bounds_padding` 1, `cache` ".box/bake", `environment` 0, `light_threshold` 2 deg, `color_threshold` 0.05, `compute` true | Probe volume of baked indirect light; adds `BakeStatus` |
| `BakeStatus` | `probes`, `baked`, `pass`, `converged`, `cache_hit`, `held`, `error` | Engine written |
| `ReflectionBake` | `resolution` 128 (8..512), `budget_ms` 1, `scene_probe` true, `scene_probe_height` 2, `environment`, `light_threshold` 2, `color_threshold` 0.05 | Cubemap reflection probes; adds `ReflectionBakeStatus` (`probes`, `resident`, `captured`, `converged`, `error`) |
| `ReflectionProbe` | `extent` {5,5,5}, `blend` 1, `priority` 0, `projection` {5,5,5} (0 = infinite), `offset` | Local probe placed by world position + rotation (scale ignored) |
| `NoBake` (tag) | | Keeps a static shape out of bake captures |

## Other components

| Component | Fields (defaults) | Notes |
|---|---|---|
| `PlanarReflection` | `resolution` 0.5 (0.125..1), `clip_offset` 0.02, `distortion` 0.02 | Mirror about the plane through the entity facing local +Y. Needs `Scene.planar_reflections`. Max `BOX_PLANAR_REFLECTION_MAX` = 2 per view |
| `FogVolume` | `extent` {5,5,5}, `density` 0.1, `color` white, `absorption` 0, `asymmetry` 0.3, `edge_fade` 1, `sun_inscatter`/`ambient_inscatter`/`light_inscatter` 1, `steps` 24 (4..128), `noise_strength` 0, `noise_scale` 4, `wind` | Raymarched box; views with `LightSettings.fog_volumes` (ctor true); max 32 per view |
| `StarField` | `count` 20000 (max 1e6), `seed` 1, `brightness` 6, `falloff` 4, `size` 1.5 px, `temperature_min` 3000, `temperature_max` 12000, `saturation` 0.6, `band` 0.4, `band_width` 8 | GPU generated stars behind all geometry; galactic plane = local XZ. Not seen by captures/IBL |
| `Environment` | `resolution` 256 (power of two 8..1024), `filter` Ggx/Box | Environment source cubemap; adds `EnvironmentStatus` (`version`, `error`) |
| `EnvironmentImage` | `path` (.hdr equirect) | Adds `Environment`; reloads on FileWatch change. Center column looks along -Z |

## Script

```flecs
using flecs.script
using box.transform
using box.geometry
using box.material
using box.light
using box.view

sun {
  Rotation: {-0.9, 0.6, 0}
  DirectionalLight: {intensity: 2.6 * math.PI}
  Rgb: {255, 244, 222}
  Cookie: {texture: clouds, size: 360, height: 140, threshold: 0.3, softness: 0.15,
    strength: 0.45, velocity: {2.4, 0.9}}
}
lamp { Position: {0, 2, 0}; Rgb: {255, 200, 150}
  PointLight: {intensity: 4, range: 8, shadows: true} }

$ {
  Scene: {baked: true, reflections: true, punctual_shadows: true, cookies: true}
  AmbientLight: {sky: {118, 130, 149}, ground: {79, 74, 68}, intensity: 0.5}
  Bake: {probe_spacing: 2.5, bounds_padding: 0.5, bounces: 1,
    capture_size: 32, budget_ms: 6}
  ReflectionBake: {resolution: 64, budget_ms: 2, scene_probe: true, scene_probe_height: 2}
}

stars { EnvironmentImage: {path: "etc/scenes/shipyard/assets/space.hdr"}
  Environment: {resolution: 1024} }
view { View: {camera: cam, lighting: {reflections: true, baked: false,
  environment: {source: stars, intensity: 0.7, background: true}}} }

mist { Position: {0, 1, 0}
  FogVolume: {extent: {17, 0.9, 5.5}, density: 0.06, color: {236, 196, 150},
    noise_strength: 0.7, noise_scale: 5, wind: {0.6, 0, 0.15}, light_inscatter: 0} }

mirror { Position: {0, 1.5, -4}; Rotation: {1.5708, 0, 0}; Box: {2, 0.05, 3}
  PlanarReflection: {} }
```

## Behavior rules

- A light lights only its scene (nearest `Scene` ancestor, else the default scene).
- A scene with no lights gets a default sun of intensity pi, unless it has a valid Atmosphere.
  A scene without `AmbientLight` gets the default sky/ground ambient.
- Only the first DirectionalLight of a scene gets cascaded shadows and its Cookie. With a Bake
  the views are shadowed by the first non-Baked one.
- Point/spot shadows need `shadows: true`, `Scene.punctual_shadows` and a view with
  `ShadowSettings.enabled`. Tiles shrink below `shadow_resolution` when the atlas is full; a
  tile re-renders only when its light, place or a caster in range changed.
- Casters: `NoShadow` (material.h) and blended shapes cast none; `LightFixture` shapes skip only
  the tiles of lights inside their world bounds (use on bulbs, lantern glass).
- Scene features gate shaders (all false by default): `baked`, `reflections`,
  `punctual_shadows`, `cookies`, `environment`, `planar_reflections`. A view samples bake and
  probes only with `LightSettings.baked` / `.reflections` (View ctor sets both true).
- `View.lighting.enabled: false` draws base color + emission only.
- Bake: a change of static shapes or lights rebakes reachable probes; dynamic shapes rebake
  none. A directional light rebakes only after turning `light_threshold` degrees or shifting
  color by `color_threshold`, so a time of day converges between rebakes; use Mixed or
  Realtime for a moving sun. `compute: false` holds the bake and keeps showing it.
- Invalid bake/probe settings set `error` in the status and bake/capture nothing.
- `PlanarReflection` replaces probe/environment specular on the shape's mesh (fades to probes
  above roughness 0.6). It does not reflect transmissive or Water shapes, fog volumes, 2D, text
  or effects; orthographic views and captures render none.
- `FogVolume` noise is in world space: moving the box does not drag the billows.
- `StarField` and `FogVolume` are invisible to probe/reflection captures and bakes.

## Cost

- Point lights render 8 octahedral octants into one atlas tile; spot lights one perspective.
- Fog: up to `BOX_FOG_VOLUME_LIGHT_STEPS` (8) cluster lookups per ray, 8 lights each; dense
  clusters lose lights. `light_inscatter: 0` skips the lookup.
- Bake is `bounces + 1` capture passes, spread over `budget_ms` per frame; cached in `cache`.
- ReflectionBake spends `budget_ms` GPU time per frame; non-resident probes do not fit.
