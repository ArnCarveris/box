# Sky and weather

A physically based atmosphere with sun, moon phases and stars, a time of day clock, atmosphere driven IBL and fog, and weather states.

## Details

The sky is rendered from precomputed transmittance and scattering LUTs and drawn where nothing else was written, with aerial perspective applied to distant geometry. The sun and moon are directional light entities referenced by the atmosphere, the moon is lit by the sun so its phase is correct, and stars are a rotating field that fades with sky brightness. A time of day component advances a simulated clock and writes the sun and moon rotations from the date and latitude, which makes reproducible captures a single component write.

The same LUTs feed the environment used for image based lighting and the height fog color, so sky, lighting and fog cannot drift apart, and a weather component blends overcast, fog and rain on top. Verification is unit tests for the sun position, the moon phase term, the fog coupling and the LUT invalidation, visual contact sheets of a full day and of each weather state, and benchmarks for the LUT, sky pass and cubemap update costs. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Atmosphere with its Rayleigh and Mie parameters, Stars, TimeOfDay and Weather, plus the CelestialLight behaviour from the light datamodel.

## References

- design/DESIGN.md, section Sky
- design/datamodel/atmosphere.flecs and design/datamodel/light.flecs
