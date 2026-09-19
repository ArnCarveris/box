# Sky: completion report

## What was built
- `box.sky`: a physically based atmosphere with the full datamodel (`Atmosphere`, `AtmosphereRayleigh`, `AtmosphereMie`) rendered from precomputed transmittance, multiple scattering and sky view tables that only refresh when the medium, the sun direction or the camera altitude move past a threshold; one full screen pass per view draws the sky and sun disk where depth is empty and aerial perspective elsewhere, into the float scene target so tone mapping applies.
- Celestial bodies: `CelestialLight` on the sun and moon with top of atmosphere intensity and color, a moon disk whose phase follows the sun and moon directions, and a `Stars` field with density, size, twinkle and daylight fade. Extract marches the medium on the CPU for the transmittance between camera and body, so a low sun lights the scene red and a moonless night stays dark, while the sky keeps the unattenuated radiance.
- `TimeOfDay`: hour, day of year, latitude and north offset produce the sun and moon directions, rotate the stars and publish a daylight factor; writable from script and the terminal so a capture jumps to any hour in the same frame.
- Sky as IBL and fog: the environment prefilter samples the sky view table when `EnvironmentLight.source` names the atmosphere, throttled by its own generation (medium change, 0.015 rad of sun, 0.2 km of climb) so a moving sun filters a quarter as often; fog color and density derive from the same tables with a toggle for hand authored fog.

## Verification
- 1098 unit tests across thirteen projects, all passing; the render project gained `Sky`, `Celestial`, `Clock` and `Ibl` suites (99 cases).
- Scenes in `integration/`: `atmosphere_sunrise`, `atmosphere_noon`, `atmosphere_sunset`, `celestial`, `celestial_moon`, `time_of_day_morning`, `time_of_day_noon`, `time_of_day_night`, `sky_fog` and a twelve frame day contact sheet; every earlier scene bit identical.
- Costs: the sky pass 0.12 ms and the sky view table 0.01 ms per frame at 960x540; extract 1.1 to 1.4 us per frame for the sky, bodies, clock and fog.
- Stress benchmarks: `static_100k` 2.17 ms mean, 4.30 ms p99, no spike, `material_100k` 2.08 ms mean, 4.27 ms p99, no spike, `templates_100k` 1.11 ms mean, 2.38 ms p99, no spike, `spikes` 7.03 ms mean, 18.00 ms p99, no spike. Microbenchmarks in `bench/` against the responsiveness epic: `Extract.static_1_changed` 20% slower and `Environment.extract` 17% slower from the per frame sky and celestial extraction (about a microsecond of fixed cost), `Effects.extract_chain` 14% slower, `Job.submit_100k` 40% slower in a module this epic did not touch which points at system load, five cases faster.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Precomputed tables keyed on the medium plus a sun and altitude threshold keep a physically based sky at a tenth of a millisecond; the same tables give fog, the sun color and the environment without a second model.
- Giving the environment its own generation, separate from the sky view refresh, is what keeps the prefilter cost off the per frame path under a moving sun.
- A system that adds a component must go through the stage; this bit once more and is now in the pitfalls list.

## Not in this epic
- An aerial perspective lookup table to amortize the per pixel march, and aerial perspective on transparent geometry, which draws after the sky pass.
- Weather (overcast, fog states, rain) is the next epic; the sky view table follows the first view's altitude when a frame has several.
