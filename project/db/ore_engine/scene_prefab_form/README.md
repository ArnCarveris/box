# Prefab form in scenes

hidden.flecs and layers.flecs write Name : Prefab {} which gives (IsA, Prefab) and a live entity instead of a prefab; use prefab Name {} and make the wrong form a script warning where possible.

## Details

`Name : Prefab { }` in flecs script creates an entity inheriting from the `Prefab` tag entity rather than a prefab, so such an entity is live: an emitter declared that way spawns at the origin. `test/integration/visual/hidden.flecs` and `layers.flecs` use it and pass by luck. Rewrite them with `prefab Name { }`, check that their baselines are bit identical afterwards (they should be, since the extra entity drew nothing that mattered; if a baseline changes, say why and refresh it), grep every scene and app script for the form, and see whether the flecs script parser can warn on `: Prefab` (a change in the flecs repository with its own test and a single line commit, never pushed).

Verification: the scenes rewritten and matching, a grep with no remaining `: Prefab` outside tests that check the warning, the flecs warning covered by a test if added.

## References

- `project/db/ore_engine/one_shot_effects/deliverable/README.md`, `/Users/sandermertens/GitHub/SanderMertens/flecs/docs/FlecsScript.md`
