# Prefab root bounds

Aggregate AABBs for prefab and template roots so a whole hierarchy can be tested as one object.

## Details

Subtree bounds are aggregated up to the root of a prefab or template instance and kept correct incrementally when a descendant moves, is added or is destroyed. Roots are recognised at instantiation rather than by walking the tree each frame, and a rigid root that only moves transforms its cached local bounds instead of re-aggregating. These aggregate bounds are the first test for picking, culling and LOD.

Verification: unit tests cover aggregation over deep hierarchies, incremental updates on add, remove, move and reparent, and the rigid root fast path. A benchmark measures the update cost when a thousand of a hundred thousand roots change per frame.
