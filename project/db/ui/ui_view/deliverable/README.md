# UI view and trees

## Built

`box.ui`: a `Ui` component roots a tree. Its update runs immediate in PreUpdate next to the
script update, creates or binds a view with an orthographic camera in logical pixels
(`Ui.height`, or the points of the surface over `Ui.scale`), walks the tree pre-order and gives
every node a `UiNode` place, the layer of its tree and a depth bias of one step per node.
`box.render` gained that bias: extract offsets the world z of an instance and of its sorted
draw, so a child is always over its parent without an authored z. Every other view hides the UI
layers and a world whose only views are UI views still draws its active camera; a UI view drops
the sun, the punctual lights, the sky and the overlay and lights its tree with a flat white
environment.

## Verified

541 tests over `test/ui` (31 new: `Tree`, `Order`, `UiView`), `test/render` (370) and
`test/view` (140); lint 0. `ui_panels.flecs` and `ui_panels_small.flecs` show the same nested
panels at 540 and 360 logical pixels over a 3D scene, and the 64 other visual scenes stay bit
identical. `bench/ui`: a dynamic 10k node tree extracts in 296 us, renumbering 10k nodes costs
378 us and only runs when the tree changes, and a static tree with one node moved extracts in
1.2 us. `bench/render` against the picking epic: `dynamic_10k` 228 to 234 us and
`static_1000_changed` 63 to 67 us, the cost of the four byte depth in the instance record.

## Left out

A node created after the update of its frame is numbered and joined to its layer one frame
later, so it can flash in the scene view; binding the layer in the hierarchy observer is the
follow up. Reordering children raises no event, so a reordered tree is marked by setting `Ui`
again. The immediate update must stay next to the script update in PreUpdate: a sync point in
another phase moves the merge of the frame and changes every scene.
