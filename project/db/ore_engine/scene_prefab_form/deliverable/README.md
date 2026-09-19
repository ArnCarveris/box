# Scene prefab form: completion report

## What was built
- `hidden.flecs`, `layers.flecs` and `instance_wear.flecs` declare their prefabs with
  `prefab Name {}` instead of `Name : Prefab {}`, which made a live entity inheriting the
  `Prefab` tag. No `.flecs` scene or script in the repository uses the old form any more.
- The flecs script parser warns on `Name : Prefab {}` (single commit in the flecs repository,
  not pushed), covered by `Eval.inherit_w_colon_prefab_warns` in flecs `test/script`.

## Verification
- The three scenes render bit identical to their baselines (max delta 0, 0 pixels differ), so
  no baseline was refreshed.
- flecs `test/script` 8018 green with the new case, box `test/script` green, lint 0 problems.

## Not in this task
- Evaluating `X : Prefab {}` after a query touched `Prefab` aborts on the constraint traits
  check in flecs; that is a separate flecs issue.
- No bench: nothing that runs per frame changed.
