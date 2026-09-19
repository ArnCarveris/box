# Tie fragile brute force pick test

Pick.matches_a_brute_force_pick_on_a_smaller_scene places identical spheres at coincident positions, so any change in entity count flips brute force against the index; make the pick order deterministic on ties and keep the test honest.

## Details

Two colliders at the same distance are a real case (stacked prefabs), and a pick should resolve them deterministically: prefer the lower entity id, or the later in the index order, and state the rule in `include/box/picking.h`; the brute force reference in the test then breaks ties by the same rule. Fix the tie rule in `src/picking` first; if the test still needs a change to its scene layout, ask before modifying it, since the project rule is that tests are not changed to make a fix pass.

Verification: the test passes with the tie rule and with a different entity count in the world (add one entity in a new case), `test/picking` green.

## References

- `test/picking/src/Pick.c`, `src/picking/pick.c`, `project/db/ore_engine/input_layers/deliverable/README.md`
