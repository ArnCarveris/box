# Layout sizing

Close the layout gaps a comparison with Clay found: grow by weight, min and max bounds, padding per side, text that wraps at the width its parent leaves, and anchors with a point of attachment on the parent.

## Details

`Layout` gains `padding_left/right/top/bottom` (a zero side takes `padding`) and `min_width`, `max_width`, `min_height`, `max_height`, applied in the measure pass. A new `Grow {width, height}` weight shares the room a row or a column leaves and stretches across it; `Text.wrap` wraps a string at the width its parent leaves and shrinks the content sized ancestors with it; `Anchor.parent_h/parent_v` put an edge of the element on a point of the parent, for dropdowns and tooltips. Trees without a Grow or a wrapping Text take the old path unchanged: the widths down, heights up and heights down passes in `src/ui/layout.c` only run when a node needs them.

Verification: unit tests in `test/ui` (Layout, Anchor, Text), the `ui_layout_sizing` visual scene, every other visual scene unchanged, and `bench/ui` `layout_10k_grown_tree` next to the unchanged layout cases.

## References

- `src/ui/layout.c`, `src/ui/text.c`, `include/box/ui.h`
