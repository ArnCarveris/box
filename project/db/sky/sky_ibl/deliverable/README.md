# Sky lighting and fog

- The environment a view prefilters from the atmosphere now has a generation of its own:
  the sky view table still follows the sun within `BOX_SKY_SUN_EPSILON`, while the cubemap
  and its prefilter only rerun when the medium changes, the sun moves `BOX_SKY_ENV_EPSILON`
  or the camera climbs `BOX_SKY_ENV_ALTITUDE`. A sun that moves 0.004 radians a frame
  filters the environment under a quarter as often as the table, and the step is small
  enough that the daylight of a scene changes smoothly.
- The fog of a view is derived from the same medium: its color is the single scattering of
  a horizontal ray at the altitude of the camera lit by the body that lights the view, plus
  the night tint, and its density is the extinction of that air per world unit, falling off
  with the scale height of the aerosol layer. `Atmosphere.fog_intensity` scales it and a
  negative value leaves an authored `Fog` alone, as does a world without an atmosphere.
- The mesh pass already adds that environment to the baked and the clustered paths alike.
- 20 new `Ibl` tests cover the trigger, the derived color at three sun elevations, the
  density, falloff and height, and both overrides. 475 `render` and `view` tests green,
  lint 0, every other scene bit identical. `sky_fog` is a hazy morning whose hills fade
  into the sky and whose mirrors reflect it. `Sky.extract_sky_fog` is 1.35 us at 1920x1080.
- Not done: the aerial perspective still marches per pixel instead of reading a table, and
  transparent geometry gets none of it.
