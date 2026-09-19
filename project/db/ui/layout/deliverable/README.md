# Layout and anchoring

## Built

The layout pass of `box.ui` gives every node of a tree a `Bounds`, the rectangle it covers in the
logical pixels of its view, centre in x, y like a `Position`. `Layout` puts the children of a node
in a row or a column in child order with padding, spacing and an alignment per axis, which moves
the group along the axis and each child across it; the box is the size of the `Layout`, else of
the `Rect`, else of the content, so a row or a column grows to fit its children, and a node with a
`Rect` has it resized to its box. `Anchor` pins a node to an edge or the centre of its parent and
takes it out of the flow. The pass reuses the pre-order walk, measures backward and places
forward, and runs only for a tree that observers on `Layout`, `Anchor`, `Rect`, `Position` and the
hierarchy marked. It lays out before it numbers: a node that gains its rectangle there gains its
instance too, and a depth bias set before that instance exists is lost.

## Verified

59 tests over `test/ui` (28 new: `Layout`, `Anchor`), `test/render` (370) and `test/view` (140);
lint 0. `ui_layout.flecs` renders the gallery below and the 67 other scenes stay bit identical.
`bench/ui`: 10k elements lay out in 481 us and a frame with nothing changed costs 36 ns.

## Left out

Whole trees are marked, so moving one node of a 10k tree lays out all of it and
`extract_10k_static_tree` went from 1.2 to 477 us; marking the changed subtree is the follow up.
A shape without a `Rect` measures zero.

![ui_layout](ui_layout.png)
