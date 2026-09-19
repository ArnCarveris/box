# UI after controller fini: completion report

## What was built
- `box_render_state_dtor` freed every render query by hand, but flecs binds a cached query to
  an entity and frees the query when that entity's table is torn down at `ecs_fini`. Table
  teardown order depends on module import order, so with `BoxController` before `BoxUi` the
  query entity died before the registry's dtor ran and the dtor hit a freed query.
- `RenderState` now records the world and the entity flecs bound to each cached query; the
  dtor skips a query whose entity is already dead and frees the uncached ones as before.

## Verification
- `Fini.controller_then_ui_finishes_cleanly` and
  `Fini.every_module_reverse_dependency_order_finishes_cleanly` in `test/render` crashed
  before the fix and pass after.
- `test/render` 415 green, lint 0 problems in the owned files.

## Not in this task
- No bench: only the world destructor changed.
