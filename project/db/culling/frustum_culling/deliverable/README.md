# Frustum culling

Extract writes a world space bounding sphere into the last 16 bytes of every
instance (`box_instance_t.bounds`) and the static slot into `params.w`, so a
compacted draw finds its baked probes. `src/renderer/frustum.h` holds the planes
and the sphere and box tests and is shared with the light culling; the far plane
of the reverse Z projection is degenerate, so a point at 1e6 is inside.

Every view culls into one visible instance buffer whose slices its draws bind. A
static batch at or below `CullSettings.gpu_threshold` (2048) is culled on the CPU
out of a mirror of its instance buffer, as are the dynamic batches out of the
packet, and a range the frustum keeps whole is uploaded straight from its source.
A larger batch gets a compute dispatch that tests the spheres, appends what it
keeps and counts it into the `DrawIndexedIndirect` arguments the draw reads back.
Layer runs still restrict the candidates and sorted draws are culled before they
are sorted. `CullSettings` reads from the view first and the singleton after; off
it leaves every previous draw path unchanged.

`static_100k` runs at 576 fps with culling (93866 drawn, 16134 culled of 110000)
and 597 fps without: compacting a scene that is 85 percent visible costs 3
percent, and an index list is the natural Hi-Z follow up. `culling.flecs` draws
14 of 37 and its capture is identical with culling on, off and forced onto the
compute path. 12 tests in `view.Frustum` and 2 in `render.Extract`, six projects
green, lint 0, wasm builds, 15 scenes bit identical, `cull_100k_cpu` 76 us.
