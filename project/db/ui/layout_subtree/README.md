# Subtree relayout

One moved node marks its whole tree for layout, so a 10k tree pays 477 us for a single change; mark and relayout only the affected subtree and its ancestors' sizing.

## Details

The layout observers mark the `Ui` root dirty, so `extract_10k_static_tree` went from 1.2 us to 477 us once a single node moves every frame. Mark the nearest `Layout` ancestor instead, walk up only while a parent sizes from content (its size can change) and stop at the first fixed size ancestor, then relayout those subtrees in tree order and renumber depth only when the structure changed. Keep the whole tree path for structural changes under the root.

Verification: unit tests for a leaf move under a fixed parent (only that subtree relaid), under a content sized chain (ancestors resize), and a structural change; `bench/ui` `layout_one_moved_of_10k` near the unchanged frame cost, every UI scene bit identical.

## References

- `project/db/ui/layout/deliverable/README.md`, `src/ui/`
