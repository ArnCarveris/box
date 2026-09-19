# Impostors

`src/render/lod_impostor.c` gives a node that is still too expensive where its box is too coarse
an atlas of eight views around its Y axis. The atlas is one render target of eight tiles; the
capture places eight turned copies of the representation the cache already holds side by side in
one orthographic view, which the extract adds at the end of the frame and moves to the head of the
packet, so the atlas is drawn before the views that show it and a node never stands in a box while
it waits. A tile is as wide as the diagonal of the footprint of the subtree and as high as the
subtree, so a turn never clips it. The billboard is one entry of the sorted list with the atlas as
its group, the same offscreen composite a UI group draws with: it turns around Y to face the
camera and shows the tile whose direction the eye stands closest to, through one quad mesh per
tile whose bounds cover the whole atlas, so the composite needs no uv transform. The impostor is
the last level before that box: its error is the size of one texel plus the depth half a turn of
eight views moves the surface by, and the walk only takes it when that error is under the
threshold of the view. It is captured again only when the content hash of the subtree changes and
its target is released when the root goes away. The shadow pass of a view is now a secondary walk
that never refines or opens what the view itself resolved.

Verified: `tests.json` 936 of 7 projects, 9 new in `lod.Impostor` over the error, the tile pick
from four directions and from a turned node, the atlas, the capture view and its batches, the
billboard draw, recapture after a change, the release on destruction and a near subtree that keeps
its parts; lint 0; 83 visual scenes match with `lod_impostors` new and every earlier scene
unchanged. `bench.json`: picking and filling 10000 billboards costs 76 us a view.
`test/integration/bench/lod_city.flecs` runs its four million leaves at 1.18 ms mean, 2.30 ms p99
and no spikes.

Left out: the views turn around Y only, so a camera above a subtree sees the side it captured; an
atlas is one target per node instead of a shared page, capped at 64; the billboard is appended
before the sorted entities instead of being depth sorted with them; and the capture takes the
lighting of the first view of the frame.
