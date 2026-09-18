# Tint opacity and composition

Tint alpha becomes the tint strength and tints compose down the hierarchy, so a color tint on a parent and a brightness tint on a child both apply to a grandchild.

## Details

`Tint {r, g, b, a}` means: mix the surface color toward the tint color by `a` (0 no effect, 255 full replacement); transparency stays with `Opacity` and the tint alpha no longer feeds the transparent pass rule. Tints compose from the root down: a child's tint applies on top of what its ancestors produced, so blue at 50% on the parent and black at 50% on the child leaves the grandchild darkened light blue. `ActualTint` keeps holding the resolved affine result, because a chain of mixes is itself one mix: with `k = (1 - a1)(1 - a2)...` the resolved alpha is `1 - k` and the resolved color is the accumulated tint contribution divided by that alpha, which the shader applies as one `mix`. Resolution runs through the hierarchy helper for ChildOf, Parent and mixed trees and re-resolves the subtree when any ancestor's tint changes.

Verification: unit tests for the mix at 0, 50 and 100 percent, composition of two and three levels including the black at 50% case, a child tint on top of an untinted parent, changes at the root and at a middle level propagating, removal of a tint restoring the ancestor result, and every hierarchy flavor (ChildOf, Parent, mixed both ways, reparenting between them); extract tests that the instance color is the mixed value; the blending tests updated so tint alpha no longer makes an instance transparent. The tint visual scene is rewritten with primary colors at 50%: single objects, each hierarchy flavor with a blue parent and a black child at 50% showing the grandchild darkened blue, checked by eye.

## References

- `src/material/tint.c`, `src/transform/hierarchy.c`, `src/render/extract.c`
- `test/integration/visual/tint.flecs`
