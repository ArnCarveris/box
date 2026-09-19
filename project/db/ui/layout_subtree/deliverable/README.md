# Subtree relayout

## Built

A change that only asks for a new layout is marked on the node it was made on instead of on the
root of its tree, and the walk resolves the mark to the nearest ancestor whose box the change
cannot resize: the parent of the node, and further up while a parent is sized by its content.
Those subtrees are laid out in tree order, one an earlier subtree covered is skipped, and the
node a pass starts at keeps the Bounds and the place its parent gave it, so the rest of the tree
is untouched and nothing is numbered again. A structural change, a pass that made or dropped a
node, a change the last walk did not place and more than `BOX_UI_MARK_MAX` changes or
`BOX_UI_SUBTREE_MAX` subtrees in one update still walk, lay out and number the whole tree. A
walked subtree drops the cached hit, and a group in one keeps the view order of its depth.

## Verified

150 tests over `test/ui` (5 new in `Layout` and `Events`), with `test/render`, `test/view` and
`test/picking` green; lint 0. The eight UI scenes, `fps_overlay` and `picking_hover` are bit
identical and the `ui_events` and `ui_text_script` command scenes pass. `bench/ui`:
`extract_10k_static_tree` 601 to 20 us, of which the tree update is 0.5 us and the rest the
extract; `layout_one_moved_of_10k` costs 0.96 us against 0.04 us for an unchanged frame and
605 us for the whole tree. The other cases are within noise of the ui_events report.

## Left out

A change under a chain of content sized rows and columns lays out the whole tree, which is what
the chain asks for, and the text of every node of a subtree is measured again with it.
