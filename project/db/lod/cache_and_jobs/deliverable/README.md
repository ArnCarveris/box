# LOD caching and jobs

`src/render/lod_cache.c` is the proxy cache the traversal owns. A node the walk would like to
replace hands the primitives of its subtree, in the local space of the node, to a job: a worker
clusters them into at most eight boxes, spheres and capsules with `box_lod_proxy_build` and, when
that set needs more shapes than its budget or carries more error than the node allows, merges and
simplifies their meshes with `box_lod_merge` and `box_lod_simplify` into one mesh that is uploaded
through the ordinary mesh cache and shared by every node whose content hashes the same. The walk
now picks the cheapest of three levels that stays under its threshold, the box over the bounds
being the cheapest and the merged mesh the finest, so a node the box already stands in for costs
one instance and a node the box misses is replaced instead of descended into; a subtree that
covers more than a tenth of the view height is never replaced at all. Four subtrees are collected
per frame, the largest on screen first, and the job queue orders them by priority and distance to
the eye, so nothing of the generation is on the frame. An entry is kept per node and only marked
out of date: the next pass hashes the primitives again and builds only when they really differ,
which is why a rigid root move costs nothing, a changed descendant rebuilds the proxies that hold
it and nothing else, and a destroyed root frees its meshes. Each view now also walks for its
shadow pass with `shadow_error_px` over the widest cascade, and `LodStats` reports the selected
levels, the proxy counts and the generation queue.

Verified: `tests.json` 927 of 7 projects, 14 new in `lod.Cache` and `lod.Shadow` over the levels,
the material fallback, the view size guard, root movement, descendant and cross root invalidation,
destruction, the job queue, the generation order, the statistics and the two shadow thresholds;
lint 0; 82 visual scenes match with `lod_proxies` new and every earlier scene, `lod_city` and
`lod_pruning` included, unchanged. `bench.json`: one 512 primitive subtree costs 19 us for its
primitive set and 4.0 ms for its merged mesh on a worker, against 47 us for the content hash the
extract pays to decide it need not build again. `test/integration/bench/lod_city.flecs` runs its
four million leaves at 1.13 ms mean, 2.05 ms p99, 2.29 ms max and no spikes.

Left out: the shadow pass fits one orthographic view over the widest cascade instead of one per
cascade; a merged mesh is built from the shapes of a finer proxy rather than from the meshes of
the leaves, so a round leaf is boxed at that level; the collection of a subtree still walks its
entities on the main thread, bounded by four subtrees and 2048 primitives a frame; and impostors,
which `hlod_proxies/impostors` owns, are not among the levels.
