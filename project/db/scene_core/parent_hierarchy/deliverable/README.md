# Parent hierarchies

`src/transform/hierarchy.c` is the one rule the engine reads a tree with: `box_hierarchy_parent`,
`box_hierarchy_children` (callback) and `box_hierarchy_collect` (vector), `box_hierarchy_ancestor_has`,
`box_hierarchy_is_child` for an observer's `other_table`, and `box_hierarchy_set_parent`, which reparents in the
storage the entity already uses. Flecs answers `ecs_get_parent`, `ecs_children`, `ecs_get_target(ChildOf)`, name
lookup, `up`/`cascade` traversal and `(ChildOf, *)` observers for both storages, so the helpers are thin. They are
used by static propagation and its reparent observer, the dynamic cascade system, tint, `box_render_is_dynamic`,
`box_render_casts_shadow`, `box_render_mark_subtree` (and with it layer changes), the AABB union of the render
command, `grep` and the shell. The dynamic system needs no second query: one `EcsUp|EcsCascade` term over `EcsChildOf`
groups Parent tables by their `(ParentDepth, n)` pair and ChildOf tables by their pair depth, so a mixed five level
chain updates breadth first in one pass. Reparenting between the storages keeps world transforms both ways, and
deleting a `Parent` node deletes its subtree and frees the instance slots.

100k entities, 50 children per node, release: `build_100k_childof` 36.9 ms in 4280 tables against `build_100k_parent`
37.8 ms in 286 tables, a 15x smaller table footprint for the same build cost; `propagate_root_childof` 4.59 ms against
`propagate_root_parent` 4.61 ms; `dynamic_subtree_parent` 76 us for 1k entities per frame;
`Extract.static_1000_changed_parent` 88 us against 67 us for flat entities, which is the ancestor walk per entity. The
five render cases over the 5% threshold drift the same with the pre-change sources, so it is machine load;
`integration.py bench` is unchanged at 2.24 and 2.12 ms.

Flecs fix (`[core] Emit ChildOf events for entities created with ecs_new_w_parent`): it never notified `EcsParent` on
set, so `(ChildOf, parent)` observers missed the children it and `@tree Parent` scripts create, leaving tint stale.

21 tests in `transform.Parent`, 16 in `transform.Mixed`, 7 tint and 5 extract tests in `render`, 5 shell and 1 layer
test; six projects green under debug and sanitize, lint 0, wasm builds, `parent_tree.flecs` renders and every other
scene stays bit identical. Limits: flecs does not copy prefab child names to instance children of a `Parent` tree, so
those are reached by iteration, not `lookup`; `Or` and `Not` over a `ChildOf` pair are incomplete for `Parent`
storage, though the root queries of `ls` and `grep` do exclude them today.
