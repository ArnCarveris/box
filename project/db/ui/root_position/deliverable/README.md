# UI root without a position

## Built

The layout pass gives every node of a tree the Position its place asks for and a node it did
not move the origin of its parent, so a root that only carries `Ui` sits at the origin of its
view and its subtree draws: a node only gains a world transform once its Position is set, and
one without it kept the zero matrix of its `WorldTransform` and collapsed its subtree.

A view now says how its blended fragments composite. A UI view hands the renderer the plain
source over contribution of an alpha blended fragment instead of the glass model the engine
draws a transparent surface with, so a glyph or an image lies over what is below it without
refracting it, without the glass specular haze around every glyph and without the copy of the
scene the view took for it every frame.

## Verified

610 tests over `test/ui` (100, 3 new in `Tree` and `Text`), `test/render` (370) and `test/view`
(140); lint 0. The 69 other visual scenes stay bit identical; `ui_text` lost the additive haze
(max delta 11, under the 24 allowed) and its baseline was refreshed. `bench/ui` is within noise
of the text task: `extract_10k_tree` 317 to 311 us, `layout_10k_tree` 516 to 505 us.

## Left out

The Position comes from the layout pass, not from an observer on `Ui`, which would reach the
root only. A glyph keeps the near black metal with its color in `Emissive`, which is what shows
its exact color under the flat white environment of its view.
