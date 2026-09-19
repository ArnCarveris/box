# Linear script edits

## Built

The flecs edit path is linear in the statement count (flecs `736e76de5`, branch
`template_values`). `ecs_script_entity_source` scanned the source from the start
for the line and column of every entity and now binary searches a line index the
script builds once; the edit set keeps a hashmap from entity and component to
edit instead of walking the edit list; the identifier of a component and the
scopes of the `using` statements are resolved once per session. `box.checkpoint`
itself is unchanged.

## Verified

`test/checkpoint` 40 tests unchanged, lint 0. In flecs `test/script` 8014 and
`test/core` 3669 tests are green, with a new `Edit.set_10k_statements` that
bounds 10k edits at 0.25 s (1.01 s before, 0.010 s after). `bench/checkpoint`
writes 500, 5000 and 50000 entities: 3.20 ms to 0.28 ms, 287 ms to 2.57 ms and
31.58 s to 27.0 ms, a flat 1.8 M entities per second. Script evaluation is still
quadratic, which a large scene pays once when it loads, not per checkpoint.
