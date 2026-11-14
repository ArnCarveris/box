# Atmosphere

Physically based sky (Hillaire 2020), planet atmospheres seen from space, cloud layers and a
sun/moon time of day. Module `box.atmosphere` (`BoxAtmosphereImport`, imports `box.light`,
`box.view`, `box.render`), header `include/box/atmosphere.h`, sources `src/atmosphere/`.
Atmosphere distances are in km.

## Components

| Component | Key fields (defaults) | What it does |
|---|---|---|
| `Atmosphere` | `sun`, `moon` (DirectionalLight entities, 0 = none), `planet_radius` 6360, `thickness` 100, `ground_altitude` 0.5, `sea_level` 0 (world Y of ground_altitude), `units_per_km` 1000, `rayleigh` {8, 1}, `mie` {1.2, 0.8, 1, {255,255,255}}, `ozone_scale` 1, `ground_albedo` {77,77,77}, `night_tint` {70,110,230}, `night_intensity` 0.003, `sun_disk_intensity` 1, `sun_radius` 0.266 deg, `moon_disk_intensity` 1, `moon_radius` 0.259, `moon_albedo` 0.12, `earthshine` 0, `aerial_distance` 32 km, `aerial_intensity` 1, `ibl_update_angle` 0.5 deg | Sky of a scene, aerial perspective, attenuates sun/moon light |
| `AtmosphereRayleigh` (script `Rayleigh`) | `scale_height` 8, `scattering_scale` 1 (0 = none) | Air molecules |
| `AtmosphereMie` (script `Mie`) | `scale_height` 1.2, `anisotropy` 0.8 (-1..1), `scattering_scale` 1, `tint` | Aerosols / haze |
| `AtmosphereStatus` (script `atmosphere.Status`) | `sun_transmittance`, `moon_transmittance` (vec3), `moon_illumination`, `lut_version`, `error` | Engine written; added/removed with `Atmosphere` |
| `PlanetAtmosphere` | `sun` (0 = first DirectionalLight of the scene), `radius` 1 (local units, times uniform world scale), `planet_radius` 6360 km, `thickness` 100, `rayleigh`, `mie`, `ozone_scale` 1, `ground_albedo`, `intensity` 1, `sun_radius` 0.266 | Shell around a planet sphere centered on the entity's world position |
| `PlanetAtmosphereStatus` | `lut_version`, `error` | Engine written |
| `CloudLayer` | `texture` (red channel read), `size` 4000, `altitude` 1500 (world Y), `offset`, `velocity` (vec2, X/Z), `threshold` 0.55, `softness` 0.15, `density` 1, `tint` | Horizontal cloud plane in the sky of the scene's Atmosphere |
| `TimeOfDay` | `hour` 12, `day_of_year` 80, `latitude` 45, `north` 0 (deg yaw; 0 = north +Z, east -X), `time_scale` 0 (sim hours per real second), `moon_offset` 0, `sun`, `moon` | Drives sun and moon Rotation |
| `TimeOfDayStatus` | `hour`, `day_of_year`, `sun_altitude`/`sun_azimuth`, `moon_altitude`/`moon_azimuth`, `moon_illumination`, `daylight` (0..1) | Engine written |

## Script

```flecs
using flecs.script
using box.material
using box.light
using box.atmosphere
using box.texture
using box.view

sun {
  DirectionalLight: {intensity: 3 * math.PI}
  Rgb: {255, 248, 236}
}
moon {
  DirectionalLight: {intensity: 0.45 * math.PI}
  Rgb: {190, 210, 255}
}
sky {
  Atmosphere: {sun: sun, moon: moon, units_per_km: 220, ground_altitude: 3.2,
    mie: {scale_height: 1.2, anisotropy: 0.8, scattering_scale: 0.8, tint: {255, 236, 214}},
    sun_disk_intensity: 8, sun_radius: 1.1}
  TimeOfDay: {hour: 17.5, day_of_year: 172, latitude: 38, north: 205, sun: sun, moon: moon}
}
cloud_h { Gen: {width: 256, seed: 17,
  layers: [{kind: GenNoise, frequency: 3, octaves: 4, gain: 0.55}]} }
cloud_high { CloudLayer: {texture: cloud_h, size: 9000, altitude: 1600, threshold: 0.7,
  softness: 0.16, density: 0.5, velocity: {6, 3}} }

$ { Scene: {environment: true, aerial_perspective: true} }

view {
  View: {camera: cam, lighting: {enabled: true, atmosphere: true,
    environment: {source: sky, intensity: 1, background: false}}}
}
```

Planet seen from space (from `etc/scenes/planet/planet.flecs`):

```flecs
$ { Scene: {planet_light: true} }
axis {
  Position: {0, 0, 0}
  PlanetAtmosphere: {sun: sun, radius: 10, thickness: 60,
    rayleigh: {scale_height: 8, scattering_scale: 1}, ozone_scale: 0}
  globe { Position: {0, 0, 0}; Sphere: {radius: 10, segments: 128, smooth: true} }
}
```

## Behavior rules

- `Atmosphere` goes on an entity of its scene (Scene root, a child, or any entity for the
  default scene). It adds `Environment {128, Box}` unless the entity has one, so the entity is an
  environment source: point `LightSettings.environment.source` at it for IBL/ambient.
- Sky and aerial perspective draw in views with `LightSettings.atmosphere` (View ctor: true).
  Aerial perspective on meshes also needs `Scene.aerial_perspective`; the environment cubemap
  needs `Scene.environment`.
- With an `Atmosphere`, the sun/moon `DirectionalLight` intensity and `Rgb` are top of
  atmosphere values: the light extract multiplies them by the transmittance toward the light
  and fades them over the disk at the horizon. The moon is also scaled by
  `AtmosphereStatus.moon_illumination`.
- A scene without lights gets a default sun of intensity pi, unless it has a valid Atmosphere.
- The environment cubemap has no sun/moon disk; it re-renders when the sun or moon turns more
  than `ibl_update_angle` or changes color by 2%.
- Invalid values (non-positive radius, thickness, units_per_km, scale height; other negative
  distances) set `Status.error` and render nothing. Check `error` first when the sky is black.
- `TimeOfDay`: one per scene (scenes put it on the Atmosphere entity). The engine writes the
  `Rotation` of its sun and moon (a prefab's Rotation becomes owned). `sun`/`moon` 0 fall back
  to the Atmosphere's, else the first DirectionalLight. Setting `TimeOfDay` restarts the status
  at its hour; `time_scale` advances it, wrapping days and years. Without an Atmosphere it
  attenuates and warms the lights toward the horizon itself.
- Clouds: at most `BOX_CLOUD_LAYER_MAX` = 4 per scene (lowest entity ids, warning for more), one
  per entity, drawn behind all geometry. Coverage `c = saturate((t - threshold) / (2 * softness)
  + 0.5)`, opacity `saturate(c * density)`. Only views drawing the atmosphere show them.
- Clouds are in the environment cubemap (seen from X/Z 0 at sea_level). Wind re-renders it only
  every `size / (64 * |velocity|)` seconds; reflection probes keep the clouds of their capture.
- Cloud shadows: a `Cookie` (LightCookie, Plane projection) on the sun with the same texture,
  `size`, `height` = `altitude`, `offset` and `velocity` moves with the layer.
- `PlanetAtmosphere`: does not attenuate any light, adds no Environment, draws no sky, disk or
  clouds; independent of the scene's `Atmosphere`, many per scene. Drawn after opaque meshes and
  blended layers around the ground (meshes of the entity or descendants whose bounds hold the
  ground sphere, e.g. a cloud sphere), before fog volumes, particles and other blended draws.
  Other transparent geometry (rings, windows) is not hazed. Captures do not include the shell.
- `Scene.planet_light`: meshes under a PlanetAtmosphere (nearest along ChildOf) get the sun
  attenuated per fragment (terminator reddening, planet shadow penumbra by `sun_radius`) plus
  sky irradiance in the twilight band. Max 63 planets per world.
- `radius` uses the uniform world scale (geometric mean); non-uniform scale is unsupported.

## Cost

- Transmittance and multi-scattering LUTs rebuild only when the medium changes
  (`lut_version` increments). Moving the sun only re-renders the environment past
  `ibl_update_angle`.
- PlanetAtmosphere marches a ray per pixel of the shell, single + multi scattering.

## Tests

- Suites `Atmosphere`, `AtmosphereSky`, `AtmosphereLuts`, `AtmosphereIbl`, `AtmosphereClouds`,
  `AtmosphereEnvironment`, `CloudLayer`, `CloudEnvironment`, `PlanetAtmosphere`,
  `PlanetAtmosphereShell`, `PlanetSurfaceLight`, `TimeOfDay`, `TimeOfDayInvalidation` in
  `test/cpu/*` and `test/gpu/*`.
