# Detail pruning

`box_lod_tree_refresh` gives every node a contribution: the edge of the square with the area of the widest face of
its bounds, scaled by the color contrast against its parent, so a part that carries the color of what it sits on
counts for a quarter of its size and a part that stands out counts for all of it. The walk projects that length
with the metric of the epic and drops a node whose projected contribution falls under the threshold of the view,
together with its whole subtree, which is why the walk never visits what it pruned. The test sits between the proxy
and the descent: a node that has a proxy keeps it, so pruning only ever drops what would otherwise be drawn in full,
and a root has nothing to be insignificant against. `LodPin` on an entity holds it and everything below it out of
the test, because a node inherits the pin of its parent when it is materialised. A pruned node hides the
renderables of its subtree exactly like a collapsed one, so the entities, their parents, their colliders and every
pick are untouched, and `LodStats.pruned` reports what one walk dropped.

Verified: `tests.json` 778 of 5 projects, 13 new in `lod.Prune` and `lod.Hierarchy` over the contribution, the
contrast, the threshold at four distances, pinning a node and a whole subtree, pruning a parent instead of its
descendants, a proxy winning over pruning, a root never being pruned, and a ray that still hits a pruned bolt; 1806
tests over every project pass, lint 0, 79 visual scenes match with `lod_pruning` and `lod_pruning_off` new and every
earlier scene, `lod_city` included, bit identical. `bench.json`: the same 512 frame city walks 28160 nodes either
way and draws 27648 instances without pruning against 3434 with it, 24214 dropped.
`test/integration/bench/lod_city.flecs` still runs its four million leaves at 0.90 ms mean, 1.44 ms p99, no spikes.

Left out: the contrast is the color of a node against its parent and ignores emission, textures and roughness; the
threshold is the `error_px` of the view, so pruning has no setting of its own; and a node that was pruned is
measured again every frame instead of being remembered, which is cheap only because the walk stops at it.
