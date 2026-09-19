# Tonemap operators

A choice of tonemap operator on ToneMapping with Tony McMapface next to the current one.

## Details

Ore Else grades with Tony McMapface, a 48 by 48 by 48 lookup applied after exposure. Add an operator
enum to ToneMapping, bake the Tony lookup into a small 3D texture at first use (or ship it under
etc), keep the current operator as the default so every baseline holds, and expose the choice in the
effect chain.

Verification: A unit test of the lookup sampling, a visual scene with the operator on beside the default, every
other scene unchanged.

## References

- project/db/effects/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/engine.flecs
