# UI root without a position

A Ui root without a Position leaves its whole subtree undrawn; a root defaults to the origin of its view so a tree draws as soon as it exists.

## Details

The pre-order walk needs a world transform on the root to place its children, and a root that only carries `Ui` never gets one. Add `Position` (and the transform components the walk reads) to the root when `Ui` is added, through the stage when it happens in an observer, and treat a root without a transform as the origin of its view. Also make glyph and image quads in a UI view composite with plain alpha over what is below them instead of the glass model of `AlphaBlend`, so the text task's near-black metal with the color in `Emissive` is no longer needed; keep that as the drawn result for existing scenes so their baselines stay identical, or refresh only the UI baselines if the compositing changes the picture and say so.

Verification: a unit test that a `Ui` root with only a shape child draws (extract produces its instance), a unit test for a text glyph's blend mode in a UI view, the UI scenes unchanged or refreshed with the reason recorded.

## References

- `project/db/ui/text/deliverable/README.md`, `project/db/ui/ui_view/deliverable/README.md`, `src/ui/`
