# Sun, moon and stars

- `CelestialLight` (`box.light`) holds the radiance a body has over the atmosphere. The sky
  marches the same medium the transmittance table is built from, on the CPU, and hands the
  view the body that reaches the camera brightest with that transmittance applied, so a low
  sun lights the scene red and a moonless night leaves it dark.
- `Atmosphere.moon` names a second light: the sky pass draws its disk as a sphere lit by the
  sun, so the phase is the geometry of the two directions and never authored. The packet
  carries the lit fraction for tests; `moon_angular_radius` sizes the disk.
- `Atmosphere.stars` names an entity with `Stars` and a rotation: a procedural field of one
  cell per hash, turned into star space by the basis of that entity, faded out between two
  sun elevations and attenuated by the air like every other body.
- Verified by 28 new `Celestial` tests over the phase term, the fade curve, the star basis
  and the light the air leaves, 427 `render` and `view` tests green, lint 0. `celestial` is a
  crescent over a sunset with the disk at the horizon, `celestial_moon` a full moon over a
  clear night. The three atmosphere baselines were refreshed: the sun transmittance makes
  their light warmer and dimmer, which is the point of the change.
- `Sky.extract_steady` is 1.15 us and `Sky.extract_moon_and_stars` 1.46 us at 1920x1080.
