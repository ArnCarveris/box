# Atmosphere rendering

- `box.sky` holds `Atmosphere` with its `AtmosphereRayleigh` and `AtmosphereMie` parts: the
  sun, the planet, the two scattering layers, the ozone, aerosol and haze absorption, the
  ground albedo, the night tint and the turbulence. A zero field takes its default, so
  `Atmosphere: {}` is Earth; `moon` and `stars` are reserved for the celestial task.
- Precomputed tables, not an analytic fit: a 256x64 transmittance and a 32x32 multiple
  scattering table that only depend on the medium, and a 192x108 sky view table that also
  follows the sun and the altitude. Extract compares the medium field by field and lets the
  sun move `BOX_SKY_SUN_EPSILON` radians and the camera `BOX_SKY_ALTITUDE_EPSILON` km first.
- One pass per view runs between the opaque and the transparent pass: the sky view table and
  the sun disk where the depth target is empty, six marched samples of the same medium where
  it is not, with the transmittance left in the alpha so one premultiplied blend covers both.
  It writes the float scene target of a chain, so tone mapping sees the sky.
- `EnvironmentLight.source` naming the atmosphere fills the environment cubemap from the sky
  view table through the new `fs_sky` stage of `environment.wgsl`.
- Without an `Atmosphere` no shader is created and no pass runs: the 46 existing scenes are
  bit identical. 23 `Sky` tests, 399 `render` and `view` green, lint 0, three new scenes.
  `Sky.extract_steady` is 17.9 us, the sky pass 0.12 ms and its table 0.01 ms at 960x540.
