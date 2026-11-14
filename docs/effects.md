# Effects

`box.effects` (`include/box/effects.h`) holds post-process settings. An effect is an entity with
one or more effect components; a `View` lists effect entities in `View.effects`
(`[{effect: e}, ...]`, `ViewEffect {enabled, effect}`) and runs them in list order on its own HDR
image only, so a later UI view stays crisp. Implementation: `src/render/effects.c` (extract),
`src/render/gpu_effects.c` (passes).

## Components (constructor defaults shown)

| Component | Key fields (default) | Notes |
| --- | --- | --- |
| `Bloom` | `intensity` 0.3, `threshold` 1, `threshold_softness` 0, `low_frequency_boost` 0.7, `low_frequency_boost_curvature` 0.95, `high_pass_frequency` 1, `firefly_clamp` 12 (0 off), `temporal` 0 (max 0.95), `max_mip_dimension` 512 (≥8) | Karis-weighted, firefly clamped; `temporal` adds ghosting |
| `DepthOfField` | `focus_distance` 10, `focus_range` 2, `near_transition` 4, `far_transition` 20, `max_radius` 0.01 (0..0.05, fraction of image height), `near_blur` true, `mode` `DofCamera`, `tilt_center` 0.55, `tilt_band` 0.08, `tilt_transition` 0.3, `tilt_angle` 0 | `DofTiltShift` blurs by screen position (miniature look, `max_radius` 0.015–0.02) |
| `Gtao` | `radius` 0.5, `falloff_range` 0.615, `final_value_power` 2.2 (0 = none), `quality` `GtaoHigh` (`GtaoLow/Medium/High/Ultra`), `denoise_passes` 1 (0..3), `half_resolution` true | XeGTAO; not a chain pass, see rules |
| `HeightFog` | `density` 0.1, `falloff` 0.3, `base_height` 0, `max_opacity` 1, `color` 191,158,140, `sun_inscatter` 1, `ambient_inscatter` 1, `atmosphere` 0 | with `atmosphere` set, in-scatter color comes from that sky |
| `AutoExposure` | `target_brightness` 0 (use range), `min_brightness` 0.1, `max_brightness` 0.3, `min_log_luma` -8, `max_log_luma` 4, `speed_up` 3, `speed_down` 1, `low_percentile` 0.5, `high_percentile` 0.95, `exposure_compensation` 0, `min_exposure`/`max_exposure` 0 (unbounded) | first frame starts at target, then adapts by frame delta |
| `Tonemap` | `auto_exposure` (entity, 0 none), `operator_` `PbrNeutral` / `TonyMcMapface` | output linear; Tony McMapface uses a 48³ LUT in `etc/luts` + dither |
| `ColorGrading` | `temperature` 0, `tint` 0, `exposure` 0 (stops), `contrast` 1, `saturation` 1, `vibrance` 0, `lift` 0, `gamma` 1, `gain` 1, `lut` 0, `lut_strength` 1 | all defaults are no-ops |
| `Smaa` | `threshold` 0.1 (0.01..0.5), `max_search_steps` 16 (1..112), `max_search_steps_diag` 8 (0..20, 0 off), `corner_rounding` 25 | SMAA 1x |
| `ChromaticAberration` | `intensity` 0.005 (fraction of width at corners), `samples` 8 (3..16), `center` {0.5, 0.5}, `falloff` 2 | |
| `Vignette` | `intensity` 0.3, `radius` 0.75, `smoothness` 0.45, `roundness` 1, `center` {0.5, 0.5}, `color` 0,0,0 | colored = tint |

Enums: `DepthOfFieldMode` (`DofCamera`, `DofTiltShift`), `GtaoQuality` (`GtaoLow` … `GtaoUltra`),
`TonemapOperator` (script `PbrNeutral`, `TonyMcMapface`).

## Script

```flecs
using box.view
using box.effects

view {
  fleet_contact { Gtao: {radius: 9, final_value_power: 2.2} }
  fleet_bloom { Bloom: {intensity: 0.2, threshold: 1.1, threshold_softness: 0.45} }
  fleet_film { Tonemap: {operator_: TonyMcMapface} }
  fleet_edges { Smaa: {} }

  View: {
    camera: orbit.arm.eye,
    clear: {enabled: true, color: {3, 5, 10, 255}},
    effects: [{effect: fleet_contact}, {effect: fleet_bloom}, {effect: fleet_film}, {effect: fleet_edges}]
  }
}
```

Auto exposure and a LUT grade (`test/integration/visual/effects_*.flecs`):

```flecs
using box.texture

exposure { AutoExposure: {speed_up: 1000, speed_down: 1000} }
tonemap { Tonemap: {auto_exposure: exposure} }
sepia_lut { Path: {path: "test/assets/luts/sepia_16.png"} }
grading {
  ColorGrading: {temperature: 0.35, contrast: 1.2, saturation: 1.1, vibrance: 0.4,
    lift: {0, 0.01, 0.03}, gain: {1.05, 1, 0.95}, lut: sepia_lut, lut_strength: 0.35}
}
main_view {
  View: {camera: camera, effects: [{effect: tonemap}, {effect: grading}]}
}
```

Tilt shift: `DepthOfField: {mode: DofTiltShift}` (strengthen with `ColorGrading` saturation ~1.3,
contrast ~1.1).

## Order and placement rules

- The view runs entities in list order. An entity holding several components runs them in a fixed
  order: `HeightFog`, `DepthOfField`, `Bloom`, `AutoExposure`, `Tonemap`, `ColorGrading`,
  `ChromaticAberration`, `Vignette`, `Smaa`.
- Typical chain: fog → DoF → bloom → (exposure) → tonemap → grading → chromatic aberration →
  vignette → SMAA.
- Before `Tonemap` (HDR): `HeightFog`, `DepthOfField` (bright highlights make bokeh), `Bloom`.
- After `Tonemap`: `ColorGrading` (LUT expects display range; before tonemap contrast pivots on
  0.18 in log space and the LUT clamps), `Vignette`, `Smaa` (edges found on clamped sRGB values).
  `ChromaticAberration` works either side (before: fringes bloom more).
- `AutoExposure` on an entity with a `Tonemap`, or referenced by `Tonemap.auto_exposure`, scales
  the tonemap input; alone it is its own pass that scales the image. The histogram skips alpha-0
  pixels and pixels below `min_log_luma`.
- `Gtao` is not a chain pass: it runs between depth prepass and forward pass wherever it is listed;
  only the first enabled one counts. It needs a lit view with a perspective camera (ignored for
  unlit, orthographic, probe/reflection captures); it scales indirect diffuse/specular of opaque and
  alpha-tested meshes only. A view whose only effect is `Gtao` renders without an HDR image.
- Skipped entries: `enabled: false`, 0, dead, or `Disabled` effect entities.

## Gotchas

- Script `{effect: e}` is enabled by the `ViewEffect` constructor; a C literal `ViewEffect` must set
  `.enabled = true`.
- Constructors set defaults, but a C literal (`ecs_set(world, e, Bloom, {.intensity = 1})`) zeroes
  omitted fields. Use `ecs_ensure` + `ecs_modified` to keep defaults.
- `DepthOfField` uses view depth along the camera forward axis; depth 0 (sky, 2D-only views) is far,
  fully blurred. `max_radius` 0 leaves the image as is.
- `ColorGrading.lut` is a 2D strip "unwrapped cube": height N, width N*N (e.g. 256x16, 1024x32).
  Any other shape logs an error once and is ignored; nothing happens until the texture loads.
- `Smaa` only anti-aliases its own view; world-space UI inside the 3D view is anti-aliased with it.
- `Bloom` first mip is half the image, or halved further with a B-spline down to
  `max_mip_dimension` (one extra pass at 1280x720, two at 2560x1440 and 4K with 512).
- Vignette alpha passes through; ChromaticAberration takes alpha from the center tap.

## Tests

`test/gpu/render` suite `Effects`; visual scenes `test/integration/visual/effects_*.flecs`
(bloom, chromatic_aberration, color_grading, depth_of_field, fog, gtao, smaa, tilt_shift, tonemap,
tonemap_tony, vignette).
