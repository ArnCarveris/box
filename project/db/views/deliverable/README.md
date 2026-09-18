# Views: completion report

## What was built
- `box.view`: `View` entities bind a camera to the surface or a `RenderTarget` texture with a clear color, a priority and flags for shadows, effects and material layers; extract writes one view block per view, synthesizes one from the active camera when no view exists, and culls lights and sorts transparency per view. Lighting and material settings are read from the view first and from their singleton after.
- Layers: a fragmenting `Layer` relationship joined with a pair, up to eight visible and eight shadow layers per view where an empty list means all, masks on instances and lights, static batches partitioned into runs per mask so restricted views draw only their runs, and `strict_layers` to draw members only. A scene without layers keeps one run and one draw call per batch.
- `render <entity|prefab|template>`: instantiates prefabs and templates in a scratch scope, frames a temporary camera on the subject's bounds, renders it alone through a strict temporary view into a 512 px target and prints the image path; cleanup follows the capture. It fixed a crash when instantiating prefabs with geometry.
- Cascaded shadows: `ShadowSettings` on a view or singleton, up to four cascades fitted with the bounding sphere of each frustum slice and snapped to texel increments, a cached static depth layer per cascade rebuilt at most one per frame nearest first with dynamic casters drawn on top, PCF sampling with cross fade between cascades, normal offset, depth bias and a distance fade; `NoShadow` and `shadow_layers` restrict casters.
- Frustum culling from the culling epic landed in between because shadows depend on it: per view visible instance buffers compacted on the CPU or by a compute pass.

## Verification
- 583 unit tests across test/core (27), test/transform (83), test/geometry (32), test/render (195), test/command (170) and test/view (76), all passing (`tests.json`, `tests_<project>.json`); the parent hierarchy task of scene_core landed before this epic closed and is included; `test/view` is new with `View`, `Target`, `Layers`, `Frustum` and `Shadows` suites.
- Visual scenes in `integration/`: `two_views`, `layers_views`, `shadows`, `shadows_low_sun`, `shadows_dynamic`, the three render command images, and every earlier scene re-rendered with shadows since all have a sun.
- Stress benchmarks with shadows on: `static_100k` 2.20 ms mean and 4.66 ms p99, `material_100k` 2.01 ms and 3.97 ms for 110k entities with 1 to 1000 changes per frame, both scenes now with a sun so shadows are measured; the shadow task measured 1.7 ms without shadows on the same build.
- Microbenchmarks in `bench/` against the materials epic: every case 5 to 23% slower including geometry and command cases no change touched, measured with a load average above 5 from system indexing after a stale test run had spun five cores for hours; the task level A/B runs put the real cost of the wider instance record (bounds and per view fields) at a few percent on `Extract.dynamic_10k`, to be re-measured on a quiet machine before the next epic.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Keeping `p->view` as a copy of the first view block let every existing test and scene stay untouched while the packet became multi view.
- Partitioning static batches into contiguous runs per layer mask costs one slot move per run behind an insertion and keeps the no layer path identical.
- A sphere fit per cascade makes the light matrix independent of camera rotation, and texel snapping makes static cascades reusable across frames; one cached rebuild per frame bounds the churn cost.
- Entities in no layer are visible everywhere by default; isolating a subject needs the strict flag, which the render command uses.

## Not in this epic
- Shadow depth arrays cost 134 MB per view at 2048 px; a smaller default or shared arrays across views are open.
- Casters more than three cascade radii away are clipped; `shadow_layers` restricts dynamic casters only within what the view draws.
- Hi-Z occlusion culling closes the culling epic next; effects and material layer toggles on the view are carried in the packet but consumed by their own epics.
