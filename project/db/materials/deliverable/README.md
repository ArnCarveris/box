# Materials: completion report

## What was built
- Blending: `BlendMode` (Opaque and eight blend equations) and `BackFaceMode` (cull, plain, textured) as inherited components. An instance whose mode is not Opaque, whose opacity times tint alpha is below one, or which shows back faces leaves the static batches for a per frame transparent list sorted back to front by view depth on the game side and drawn after the opaque pass with one lazily built pipeline per blend and cull combination, depth write off. Opaque textured alpha is a cutout.
- Milk glass: the opaque target is copied into a mip chain after the opaque pass; alpha blended surfaces sample it at a mip chosen by roughness, so a frosted panel blurs what is behind it and a clear one does not.
- Material layers: `MaterialLayer` (a WGSL function body and an order) and `LayerParams` (two vec4) on layer entities, enabled per material with the inherited pair `(MaterialLayerOn, layer)`, up to sixteen layers packed into spare instance bits, `MaterialLayerSettings.enabled` as the global toggle until views exist. The renderer recomposes the mesh shader and rebuilds its pipelines when the registry changes and keeps the old ones when a body fails to compile. Built in layers under `box.material.layers`: dust, edge wear, grime and wetness, all procedural with a shared noise function and a `Surface` struct that includes the normalized local position. A project adds a layer by creating an entity with a body.

## Verification
- 436 unit tests across test/core (27), test/transform (46), test/geometry (32), test/render (181) and test/command (150), all passing (`tests.json`, `tests_<project>.json`); the render project grew from 148 to 181 tests (`Blending` 16, `Layers` 17).
- Visual scenes in `integration/`: `blending` (overlapping panels in several modes, sorted), `milk_glass` (frosted and clear panels over textured geometry), `back_faces` (the three modes), `layers` (a textured crate and sphere plain, with each layer and with all four), and every earlier scene unchanged.
- Stress benchmarks: `static_100k` 1.47 ms mean and 3.33 ms p99, `material_100k` 1.36 ms and 2.82 ms for 110k entities with 1 to 1000 changes per frame.
- Microbenchmarks in `bench/` against the textures epic: `Bake.churn_1000_of_100k` 6.6% slower and `Bake.invalidate_one_of_100k` 10% faster, both sub microsecond cases at the noise floor. New cases: `Blending.transparent_10k` 462 us, `Layers.extract_10k_with_layers` 510 us against 195 us for the plain dynamic extract.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Keeping transparency out of the static slot machinery and rebuilding a sorted list per frame is simpler than sorting inside batches and costs half a millisecond for 10k instances.
- Shader recomposition at runtime is cheap enough to be the extension point for project layers; keeping the previous pipelines on compile failure makes a broken body harmless.
- Registry flags set by the first use keep every new feature at zero cost for scenes that do not use it; this is now the pattern for extract.
- One existing test had to change: a translucent instance is no longer a static delta but a transparent list entry.

## Not in this epic
- The layer toggle per view and the order independent transparency of overlapping surfaces within one mesh wait for the views epic and later work.
- Layer bodies must keep metallic in range themselves; there is no clamp after layers.
- Scratches from the brief's example list are not a built in layer; a project can add one with a body.
