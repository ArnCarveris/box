# Weather

Weather states that blend overcast, fog and rain on top of the atmosphere.

## Details

A Weather component holds overcast, fog and rain amounts that blend on top of the sky: overcast blends the sky towards a plain color and flattens the sun, fog raises the height fog density, and rain drives a particle emitter and a wetness material layer. The values are continuous and transition smoothly, so an agent can set one from a command and still get a stable frame. The atmosphere remains the base, so the sun keeps moving under overcast, and the rain particles themselves ship as the rain task of the particles epic.

Verification is unit tests for the blend between clear and overcast, the fog coupling, the emitter and wetness driving and the transition behaviour, visual contact sheets of the clear, overcast, foggy and rainy versions of one scene, and a benchmark that checks heavy rain stays inside the particle budget. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Weather.

## References

- design/DESIGN.md, section Sky
- design/datamodel/atmosphere.flecs
