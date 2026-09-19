# Pick test ties: completion report

## What was built
- Two colliders at the same distance along a ray now resolve to the lower entity id in
  `src/collision/index.c`, so the pick does not depend on the order the index visits them.
  The rule is stated on `box_picking_pick` in `include/box/picking.h` and on `box_ray_hit_t`.
- The brute force reference in `Pick.matches_a_brute_force_pick_on_a_smaller_scene` breaks ties
  by the same rule; the scene layout is unchanged. It placed neighbouring prefab children on
  coincident positions, which is what made the case flip with the entity count.

## Verification
- `Pick.equal_distance_resolves_to_the_lowest_id`: two coincident spheres, the earlier id wins
  whichever was set last.
- `Pick.matches_a_brute_force_pick_with_one_more_entity`: the brute force scene with one extra
  entity created before the prefabs, so every id shifts by one.
- `test/picking` 51 green, `test/collision` 89 green, lint 0 problems.

## Not in this task
- No bench: the tie comparison adds one branch to a hit that already happened.
