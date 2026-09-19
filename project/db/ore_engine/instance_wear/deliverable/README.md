# Instance wear: completion report

## What was built
- `Wear {dust, grime, edge, wet}` and `WearDetail {burn, scratch, seed, scale}`: per entity factors
  of the amounts the scene set on the material layers, plus the noise offset and the feature
  frequency of that one entity. Both are inherited by the instances of a prefab, both have a
  constructor whose values are neutral, so a channel an author does not name leaves its layer where
  the scene put it, and a channel of zero keeps a layer off the entity. Four floats each, which is
  what makes both transitionable.
- Two new instance words carry the six amounts, the seed and the scale as bytes, with one bit of the
  mode word saying they hold anything; an instance without the components keeps them at zero and the
  shader reads the neutral wear, so a scene that does not use them is bit identical. A registry flag
  keeps the extract out of the resolution entirely until the first component appears.
- `Surface.wear` in the shader is how a layer body reads them, and `wear_noise(s, frequency)` folds
  the seed and the scale into the noise of any layer, project ones included. The four layers box
  ships take their amount times their channel and their noise through that helper.
- A transition whose only moving record is on a component the renderer has no channel for, such as
  `Wear`, now puts its instance on the slow path, which is what makes a wear transition work on a
  static entity; an entity that also transitions its pose keeps the renderer records as before.

## Verification
- 752 tests over `test/render` (413, new `Wear` suite of 10), `test/transition` (40), `test/view`
  (147), `test/shader` (30), `test/effects` (7) and `test/geometry` (115), all passing. The suite
  covers the neutral instance, the amounts and the detail in the packet, the clamps, re-extraction
  on a write, the values surviving a slot move, prefab inheritance, a dynamic instance, a live
  transition and the generated shader reading the channel.
- All 95 visual scenes pass; `instance_wear` is new and `layers`, `showcase` and every other scene
  are bit identical.
- `bench/render` against the resolution_scale report on the same machine: `Extract.dynamic_10k`
  260 to 265 us and `Layers.extract_10k_with_layers` 604 to 617 us, both at the noise floor, so a
  scene without the components pays nothing. The new `Wear.extract_10k_with_wear` is 704 us for the
  same 10k instances carrying both components, which is the cost of the channel when it is used.
- `python3 tools/lint.py` reports 0 problems.

## Not in this task
- The scratch, grain, streak and burn layers are left to the game as bodies: adding them to
  `box.material.layers` renumbers the layer bits and breaks the counts three `test/render` `Layers`
  cases assert, and the project rule is that tests are not changed to make a change pass. The
  `burn` and `scratch` channels are cut, so such a body reads its per entity amount today.
- `test/picking` `Pick.matches_a_brute_force_pick_on_a_smaller_scene` still fails, as it did after
  `ore_engine/resolution_scale`: new reflected members shift entity ids and flip a tie between two
  coincident colliders. That is the filed `ore_engine/pick_test_ties` bug.
- An entity that transitions its pose and its wear at once keeps the renderer records for the pose
  and leaves the wear to the frames it is extracted.
