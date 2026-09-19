# Hiding an entity without deleting it

A Hidden tag that keeps an entity and its subtree out of every view and every pick while it stays in the world, so ghosts, pooled effects and world anchored elements are shown and hidden instead of created and destroyed.

## Details

Box has no way to take an entity out of the frame short of deleting it or stripping its shape: the
extract queries match disabled entities on purpose, `Opacity` still draws and a layer switch is a
sledgehammer. Ore Else needs one everywhere. Placement paints a drag of up to a few hundred ghost
tiles that changes every frame, which the old engine drew with the immediate mode
`flecsEngine_draw`; the box way is a pool of ghost entities that are shown and hidden, and that needs
a switch. The world anchored elements of `ore_engine/world_anchor` hide when the target is behind the
camera, a pooled effect of `ore_engine/one_shot_effects` waits hidden between plays, and a UI tree
hides a panel the same way. Add an inherited `Hidden` tag that the extract, the shadow pass, the UI
walk and the pick all skip for the entity and everything below it, cheap enough to toggle on
hundreds of entities a frame (a tag change per entity, no slot churn), and say what it does to an
emitter that is playing and to a light.

Verification: Unit tests that a hidden entity and its subtree leave the packet and the pick and come back
unchanged, a bench case toggling a thousand entities a frame, a visual scene with half a grid hidden,
every other scene unchanged.

## References

- project/db/views/deliverable/README.md
- project/db/picking/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/placement.c
