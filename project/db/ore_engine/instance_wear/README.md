# Per instance material layer parameters

A component that feeds a material layer with per entity amounts, the box answer to flecs-engine SurfaceWear: an entity says how dusty, grimy, chipped and burnt it is and the layers of its material read it, so a wall that takes damage chars and a rock never scratches.

## Details

Box enables a layer per material with the inherited pair `(MaterialLayerOn, layer)` and parameterizes
it with `LayerParams`, which is one value for the whole scene. Ore Else instead carries
`SurfaceWear {dust, grime, scale, seed, burn, edge, scratch}` on 209 entities and drives `burn` from
health and `dust` from the age of a building, which box cannot express at all. Add a per entity
component (`LayerWear` or `LayerInstance`) whose floats the extract packs into spare instance lanes
the way the layer bitmask already is, read by the built in dust, edge wear, grime and wetness layers
and by any project layer through the same `Surface` struct; a `seed` that offsets the noise so two
copies of a prefab differ, a `scale` that keeps the feature size in metres whatever the instance
scale, and per entity switches that keep a layer off for glass or rock. Keep an entity without the
component exactly as it renders today, and add the scratch, grain, streak and burn layers the game
asks for to `box.material.layers` while the channel is being cut, or leave them to the game as
bodies and say so.

Verification: Unit tests that the amounts reach the packet and survive an instance move, a visual scene with the
same prefab at several wear levels beside a clean one, every other scene unchanged.

## References

- project/db/materials/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/include/modules/surface_effects.h
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/buildings
