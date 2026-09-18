# Parent hierarchies

Support the non fragmenting Parent relationship next to ChildOf in transforms, bounds, tint, layers and extraction.

## Details

Flecs offers `EcsParent` as a non fragmenting alternative to ChildOf with query traversal, cascade ordering and name lookup; the brief asks for both, and the million object scenes need it because fragmenting a scene of tiny children into as many tables is expensive. Transform propagation, bounds aggregation, tint resolution, layer inheritance, the shell commands and extraction learn to read either relationship through one traversal helper so the rule lives in a single place. Mixing both in one tree is allowed: a ChildOf subtree under a Parent node and the reverse, and reparenting between the two keeps world transforms.

Verification is a test matrix in test/transform mirroring the existing ChildOf suites for Parent and for mixed trees: static parent with static children, dynamic parent with static children, static parent with dynamic children, dynamic subtrees under static roots, a dynamic node between two static levels, reparenting a static child from a ChildOf parent to a Parent parent and back, deleting a Parent node with children, and prefab instantiation of Parent trees; each case checks world transforms after the frame and that only dynamic entities are re-uploaded, and the same matrix covers material tint: Tint set on a Parent node resolves into ActualTint on its children and grandchildren, changes propagate, a child's own Tint wins, and mixed trees resolve through both relationships. A benchmark compares table counts, transform propagation and extraction for a 100k entity tree built with each relationship, and the stress scenes stay above 60 fps.

## References

- flecs `docs/HierarchiesManual.md`, `EcsParent`, `EcsParentDepth`
- `src/transform/static.c`, `dynamic.c`, `src/render/extract.c`
