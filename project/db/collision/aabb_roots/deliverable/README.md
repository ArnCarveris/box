# Prefab root bounds

## Built

The `box.collision` module with a `RootBounds` component. Prefab instances
(`IsA`) and script template instances (the pending marker) are recognised at
instantiation and turned into roots by the flush that runs in `OnStore`.
Observers on `AABB`, on both parent storages and on the world transform mark the
root a change belongs to, so the flush walks only the roots that changed. A root
with `RigidSubtree` that moved transforms its cached local bounds instead of
aggregating its subtree; a nested root contributes its own aggregate.

## Verified

`test/collision` suite `Roots`, 20 cases: prefab and template recognition, deep
and both-storage aggregation, add, remove, move, reparent, nested roots, local
bounds and the rigid fast path. `bench/collision` suite `Roots`: an idle frame
with 100k roots costs 14 us, 1000 changed roots of 100k cost 475 us and 1000
rigid moves 351 us. Linter clean.

## Left out

Bounds still come from the `AABB` the renderer writes, so a headless world
without `box.render` sees only the bounds a scene sets itself.
