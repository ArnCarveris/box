# LOD hierarchy traversal

`box_lod_tree_t` is the hierarchy the renderer owns: every `RootBounds` root is a node, `box_lod_tree_cluster`
stacks renderer only clusters over those roots until a handful are left, and the authored subtree of a root is
materialised only while a walk descends into it. A root that appears is walked once, which hides its renderables,
sums the color its proxy is drawn in and measures the shell its bounding box misses, the error the metric of
`screen_space_error` projects. `box_render_lod_update` runs at the head of the extract and walks the tree once per
view: a node outside the frustum stops there, a node the coarse depth grid of the renderer hides stops at its
proxy, a node under the threshold of the view is drawn as one box and releases and hides its subtree, and every
other node descends; descending wins over the views that would collapse. A hidden leaf gives up its static slot and
gets it back when a walk reaches it again, so the entities, their parents, their colliders and every pick are
untouched. `LodPin` and `LodStats` are the new components, the grid comes back on the packet from one small level
of the Hi-Z pyramid, and nothing is ever dropped on it because a hidden node still leaves its proxy.

Verified: `tests.json` 880 of 6 projects, 22 new in `lod.Tree` and `lod.Hierarchy` over traversal order, the
culling short circuit, clustering, the band, ECS identity and a ray that still hits a hidden leaf; 1798 tests over
every project pass, lint 0, 77 visual scenes match with `lod_city` and `lod_city_off` new. `bench.json`: a walk of
a 4096 root city visits 72 nodes whether it holds 32k or 256k leaves, and 92k when the camera stands inside it.
`test/integration/bench/lod_city.flecs` runs four million leaves under 50000 roots at 0.82 ms mean, 1.17 ms p99 and
no spikes, drawing 36k instances.

Left out: a proxy is one box over the bounds of the node, so the reduced primitive sets and merged meshes of
`hlod_proxies` are still unbuilt; generation and release run on the extract thread, not the job system, which
`cache_and_jobs` owns; the shadow threshold of a view is extracted but unused; and opening a node refines its own
error without re-aggregating its ancestors until the roots change.
