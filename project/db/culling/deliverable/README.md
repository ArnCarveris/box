# Culling: completion report

## What was built
- Frustum culling: a bounding sphere per instance next to its transform, six planes per view shared by the CPU, the GPU and the light culling (`src/renderer/frustum.h`), CPU compaction of the visible set for batches under `CullSettings.gpu_threshold` (2048) and a compute pass writing indirect draw arguments above it. The visible set is a compacted instance buffer, so shaders and pipelines stayed untouched and every scene rendered bit identical with culling on, off and forced to the compute path.
- Hi-Z occlusion culling: per view, a first pass draws what the previous frame's depth pyramid accepted, its depth is reduced into an `r32float` mip chain with the reverse Z minimum rule, and a second pass tests every instance against the new pyramid and draws what it uncovers. Nothing is dropped on stale depth, so a fast camera shows every object; a 120 frame sweep past walls is byte identical with and without occlusion. Only compute culled static batches take the occlusion path; dynamic, transparent, small and shadow batches keep the frustum path.
- `CullSettings` on a view with singleton fallback: `enabled`, `gpu_threshold`, `occlusion`, `hiz_levels`. Occlusion defaults to off: on the 20k building city it hides 12669 of 20001 instances but the frame goes from 0.25 to 0.80 ms because boxes are cheaper to draw than the pyramid and the two tests; scenes with expensive shading enable it per view.

## Verification
- 726 unit tests across ten projects (core 27, transform 83, geometry 32, render 223, command 170, view 90, job 20, input 40, monitor 23, script 18), all passing; the job, input, monitoring and script modules landed while this epic was open and are included; `test/view` gained `Frustum` and `HiZ` suites (14 Hi-Z cases: reduction rule, sphere projection, mip selection, a CPU twin of the occlusion test).
- Visual scenes in `integration/`: `culling` and `occlusion` identical to their no culling twins, plus every earlier scene unchanged.
- Bench: the scene bench app reports drawn, culled and occluded counts; `city.flecs` on and off in `city_*.json`; stress scenes `static_100k` 2.29 ms mean and 4.38 ms p99, `material_100k` 2.20 ms and 4.21 ms, `templates_100k` 1.08 ms and 2.06 ms, all with shadows on.
- Microbenchmarks in `bench/` against the views epic: eight cases 5 to 11% slower, spread over modules this epic did not touch, at the noise level of the views baseline which was itself taken under load.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Compacting instance data instead of indexing it kept the draw path identical and every pipeline untouched; the price is bandwidth, which the counts show is small.
- On wgpu-native a texture read recorded before the render pass that fills it reads nothing; the first occlusion pass needs its own command buffer.
- Occlusion culling only pays when shading is expensive; the counts are the win to keep, the default should follow the cost.

## Not in this epic
- LOD traversal integrates with the visible sets later; shadows and transparency are frustum culled only.
- Sub threshold static batches are not occlusion culled; a CPU pyramid readback would be needed.
