# Linear script evaluation

## Built

Script evaluation is linear in the statement count (flecs `e1b0775d9`, branch
`template_values`). The dependency pass compared every component statement with
every earlier one through `flecs_script_dep_entity_may_match`, which strcmp'd
the name chain, so a scene paid the square of its statements at load. Each owner
is now indexed by a hash of the entity path it names, with a bucket per entity
symbol and a list for entities named by an expression, merged in source order.

## Verified

flecs `test/script` 8016 and `test/core` 3669 tests are green, with a new
`Eval.eval_10k_statements` bounding a 10k statement managed script at 1 s (1.53
s before, 0.17 s after, debug). The new `bench/script` `Load` suite: `scene_10k`
755 ms to 42.1 ms, `scene_50k` 18.80 s to 216 ms, before run in
`bench_before.json`, `Incremental` unchanged. `templates_100k` holds 300 frames
at mean 1.43 ms, p99 2.74 ms. box `test/script` 21 pass, lint 0.
