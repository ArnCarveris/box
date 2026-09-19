# Hierarchical LOD: completion report

## What was built
- `box.lod`: `box_lod_error_px` projects a world space error at the nearest point of a node's bounds for perspective and orthographic cameras, and `box_lod_select` holds a level until the error leaves a hysteresis band. `LodSettings` (enabled, quality, error_px, shadow_error_px, hysteresis) sits on a view with the singleton as default and the extract writes its scaled thresholds into the packet view.
- Proxy generators: `box_lod_proxy_build` clusters a subtree's primitives by material and proximity into a few boxes, spheres and capsules with an approximation error in world units, allocation free through a reusable builder; `box_lod_merge` and `box_lod_simplify` merge a subtree per material group into one world space mesh and simplify it with meshoptimizer (vendored as a bundle, MIT) under an error bound with the measured error returned.
- Primitive reduction: the authored segment count of a parametric shape becomes a ladder halving down to a floor per shape family, each level an ordinary mesh cache entry shared by every instance at that level, selected per frame per view from the chord error at the instance radius, so a level change is a handle swap.
- Traversal: `box_lod_tree_t` is the renderer's own hierarchy, collision roots as nodes with renderer only spatial clusters stacked above them (no `Parent` or `ChildOf` touched) and authored subtrees materialised only while descended into. Once per view at the head of extract the walk rejects a node by frustum or the Hi-Z depth grid, resolves a node under the threshold to its proxy and releases its subtree's slots, or descends; descending wins across views, and entities, colliders and picks are untouched.
- Detail pruning drops a node whose projected contribution (widest face scaled by color contrast against the parent) is below the view threshold, together with its subtree, unless it or an ancestor carries `LodPin`; a proxy wins over pruning and roots are never pruned.
- Caching and jobs: a proxy per node is cached in the node's local space and content hashed, so a rigid root move rebuilds nothing, a changed descendant rebuilds only the proxies holding it and destruction frees; a job clusters the subtree into at most eight primitives and falls back to a merged and simplified mesh when that misses more than the node allows, with four subtrees collected per frame largest first and jobs ordered by distance, the box placeholder drawn until the proxy arrives. The walk picks the cheapest of four levels under its threshold, shadow passes walk as a secondary pass with `shadow_error_px`, and `LodStats` reports levels, proxy counts and queue depth.
- Impostors capture eight views around the Y axis of a subtree into one atlas through the offscreen composite the UI groups use, inserted at the head of the packet so the billboard is never a frame late; the billboard is one sorted draw picking the nearest tile, the last level before the box, selected only when its texel and parallax error fits and recaptured only on invalidation.

## Verification
- 1834 unit tests across 20 projects, all passing; `test/lod` is new with 151 tests over the `Cache`, `Cluster`, `Degenerate`, `Determinism`, `Error`, `Hierarchy`, `Hysteresis`, `Impostor`, `Material`, `Merge`, `Prune`, `Reduce`, `Screen`, `Shadow`, `Simplify`, `Tree`, `Walk` suites.
- Scenes in `integration/`: `lod_spheres` (the segment ladder across distances), `lod_city` against `lod_city_off`, `lod_pruning` against `lod_pruning_off`, `lod_proxies` (generated primitive sets and merged meshes replacing subtrees) and `lod_impostors` (billboards at extreme distance); every earlier scene bit identical. `lod_city.json` records four million leaves under 50k template roots with a flying camera at 1.2 ms mean and 2.3 ms p99, no spikes.
- Benchmarks in `bench/` against the monitoring epic: 100k error projections in 357 µs, a 1000 primitive proxy in 69 µs, a 100k triangle merge in 5.3 ms and simplify in 9.0 ms, level selection for 10k spheres in 179 µs taking 40M triangles to 1.3M, a walk visiting 72 nodes for 32k and 256k leaves alike, and pruning taking 27648 drawn instances to 3434.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Reusing the collision module's incremental root bounds as the LOD roots kept the hierarchy free of a second aggregation pass.
- Materialising authored subtrees only while descended into is what makes the walk independent of the leaf count.
- Reduction stays opt in through `LodSettings`: switching it on by default would have changed every existing baseline.
- Hash mixing matters: FNV alone made the weld table of the merger 430 times slower.

## Not in this epic
- Splines rebuild per entity and do not join the segment ladder; icospheres count subdivisions rather than segments.
- Contrast for pruning uses color only, not emission or textures, and pruning has no threshold of its own beyond the view's error.
- Generated proxies only replace a node covering under a tenth of the view height, which caps impostors at about 54 px; the shadow walk never refines what the view resolved. Generation still ignores emission and textures when it picks the proxy color.
