# Scene core: completion report

## What was built
- `box.transform`: Position, Rotation, Scale, Orientation, LookAt, WorldTransform, AABB; observer driven static updates, per frame cascade updates for `DynamicTransform` subtrees, ManualTransform.
- `box.geometry`: 18 shape components backed by 17 procedural mesh generators with normals, UVs and bounds; a mesh cache keyed on topology so radius and size live in the instance scale.
- `box.material`: Rgb, Opacity, Tint with recursive ActualTint, PbrMaterial, Emissive, DynamicMaterial.
- `box.camera` and `box.light`: Camera with reverse Z perspective and orthographic projections, Billboard, DirectionalLight, PointLight, SpotLight components.
- `box.render`: mesh registry, static instance slots with swap removal, dirty tracking through observers, per frame dynamic extraction, camera and sun extraction into the frame packet, debug validation.
- Renderer: mesh table, per mesh static instance buffers grown on the GPU, shared dynamic buffer, instanced indexed draws, lit WGSL shader.
- `box.movement`: Velocity3 and AngularVelocity3 integration, marking entities dynamic.
- `apps/box_bench` and `tools/integration.py` for visual and benchmark integration scenes.

## Verification
- 130 unit tests across test/core, test/transform, test/geometry and test/render (`tests.json`).
- Visual: `renderer/deliverable/primitives.png` renders every shape with materials and lighting; `stress/deliverable/static_100k.png`.
- Benchmarks (release, 1280x720 headless, 300 frames, 1 to 1000 static changes per frame): 110k entities at mean 1.25 ms (p99 2.6 ms) for the static scene and mean 1.23 ms (p99 2.6 ms) for the material scene, see `stress/deliverable/*.json`.
- Wasm build under `deliverable/wasm`, open `box_app.html#etc/scenes/primitives.flecs`.

## Lessons
- This flecs has no implicit defer mode: OnAdd and OnRemove observers must queue structural changes through `it->stage`; OnSet observers may write directly. Deletion presents the empty root table as `other_table`.
- Private components need an explicit zeroing ctor; a stale dirty flag from uninitialized memory cost an hour.
- Crashing test children are slow on macOS because the crash reporter symbolicates each one; keep tests from crashing rather than tuning the harness.
- bake did not relink `box_app` after the library changed in one instance; `bake3 build box_app` relinked. Worth a look in bake3.

## Not in this epic
- View and layer components, frustum and occlusion culling, textures, shadows, tone mapping. Colors currently clip when albedo times sun exceeds one.
- Mesh3 custom meshes and the 2D Border, Line, Image shapes.
