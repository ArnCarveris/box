# Visibility: completion report

## What was built
- `box.render.Hidden`, an inherited tag that takes an entity and everything below it out of the
  frame while it stays in the world. A hidden prefab hands the tag to its instances and a hidden
  entity hides its subtree; both are resolved from the ancestor walk, gated by a registry flag so a
  world that never adds the tag pays nothing.
- Render: a hidden renderable gives its static slot back, leaves the dynamic array, the sorted
  transparent list and every shadow pass, and takes its slot again when it is shown; a hidden point
  or spot light leaves the packet and the bake; a hidden directional light hands the sun of the view
  to the next one; a hidden emissive instance leaves the probe bake.
- Collision and picking: a hidden root comes out of the aggregate empty and leaves the spatial
  index, a hidden part shrinks the bounds of its root, a hidden loose collider and a hidden mover
  leave their trees, so every ray, sphere, box and frustum query and every pick misses them at no
  per query cost. The UI hit test skips a hidden node, so a hidden panel hands the pointer back.

## Verification
- 861 tests over `test/render` (396, new `Hidden` suite of 10), `test/picking` (49, new `Hidden`
  suite of 8), `test/collision` (89), `test/ui` (180) and `test/view` (147), all passing.
- `hidden` baselined; all 92 visual scenes pass, the other 91 bit identical.
- `bench/render` attached with the new `Hidden` suite: toggling 1000 of 10k entities a frame costs
  185 us, a 10k dynamic extract with a hidden tenth 232 us.
- `python3 tools/lint.py` reports 0 problems.

## Not in this task
- `box.particle` does not read the switch: a hidden emitter keeps its clock, keeps spawning and
  keeps drawing. Wiring it in is a follow up on `src/particle`, which another task owns.
- The UI layout pass still runs for a hidden subtree, which is what makes showing it instant.
- A LOD node whose whole subtree is hidden still holds a proxy of zero volume.
