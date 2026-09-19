# Post process effects: completion report

## What was built
- `Shader` component: WGSL files under `etc/shaders` with includes, macros and conditionals, a search path (working directory, the app folder, the executable folder), reload as a component write with the previous pipelines kept on failure, error reporting with file and line; every embedded shader string moved into files and the material layer composition goes through a generated include. `box.shader.builtin` entities expose the engine shaders so a macro on the mesh shader is a script line.
- Effect graph: effects are entities under `box.effects.stack` with an `Effect` (shader, order, scale, enabled, inputs) and their parameter component; a view runs its `effect_list` or the whole stack, `View.effects` disables it. The renderer pools intermediate targets by size and format, resolves the declared inputs (color, depth, an RGBA16F normals attachment with the indirect diffuse and specular fractions, motion vectors, an exposure buffer, a color pyramid) only when asked, and a view with a chain renders the scene into an RGBA16F target so the chain sees unclipped color, with the gamma encode moved to the last pass.
- Effects: auto exposure from a 256 bin log luminance histogram with separate adaptation speeds and clamps; tone mapping with a hue preserving neutral curve and 8 by 8 ordered dithering; bloom on the 13 tap and tent pyramid with Karis firefly reduction and no threshold; analytic height fog; GTAO at half resolution with a depth aware upsample and a tangent plane denoise, scaling only the indirect diffuse fraction; screen space reflections with a side change hit test and a confidence fade into the prefiltered environment, replacing the indirect specular fraction. The built in order is fog, exposure, bloom, tone mapping.
- A showcase scene with every effect on over transparency, glass, layers, blend modes, PBR textures, emissive objects and shadows, which caught the sorted pass leaving the normals attachment untouched so occlusion and reflections treated blended pixels as the surface behind.

## Verification
- 889 unit tests across twelve projects, all passing (`tests.json`, `tests_<project>.json`); `test/shader` is new with 24 tests and `test/view` grew to 140 with the `Effects` suite at 50.
- Scenes in `integration/`: `shader_macro`, `effect_toggle`, `tonemap`, `auto_exposure`, `bloom`, `height_fog`, `gtao` and `gtao_off`, `ssr` and `ssr_off`, `showcase` and `showcase_off`, all baselined, every earlier scene unchanged.
- Pass costs at 1280x720 (mean CPU frame time, effect on against off): tone map 0.21 against 0.20, bloom 0.33 against 0.16, fog 0.15 against 0.13, GTAO 0.48 at half resolution against 0.20, SSR 0.27 against 0.20, the full chain on the showcase 1.20 against 0.46 ms.
- Stress benchmarks: `static_100k` 2.25 ms mean and 4.68 ms p99, `material_100k` 2.16 ms mean and 4.56 ms p99, `templates_100k` 1.11 ms mean and 1.80 ms p99. Microbenchmarks in `bench/` against the culling epic: `Svg.rasterize_256` 58% slower which is the resvg cost recorded in its task, `Effects.extract_chain` 24% slower with six built in effects instead of one, `Textures.extract_1000_materials` 24% and `Environment.extract` 14% slower from the texture back references the SVG jobs added, the static extract cases 9 to 10% slower, two cases faster; the stress scenes unchanged.
- Build reports `build_debug.json`, `build_native.json` and `build_wasm.json` from bake3; wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Moving shaders into files with a preprocessor made the layer composition and every later effect a component plus a WGSL file with no renderer code.
- Tone mapping and bloom are meaningless on a clipped 8 bit scene; the HDR target had to come before the effects were judged.
- Sample positions must be snapped to depth texel centres on grazing surfaces or the horizon search bands; a hit test by change of side with the thickness as confidence avoids the miss speckle of a thickness window.
- A composition scene finds what per effect scenes cannot: the blended pass must write the g-buffer fractions or later passes read the surface behind it.

## Not in this epic
- SSR marches the depth target, not the Hi-Z pyramid, which keeps the farthest depth per region and cannot bound a march from the near side.
- Motion vectors are camera only; GPU timings of the passes wait for the monitoring task that depends on this epic.
- Effects on texture target views and the sky as an environment source come with the sky epic.
