# Frustum culling

Per view frustum culling over renderer owned instance bounds, feeding indirect draws.

## Details

Extract writes a world space bounding sphere and AABB per instance next to its transform so the renderer can cull without touching flecs. The renderer builds the six frustum planes per view from the view projection matrix and produces a visible instance list per mesh, on the CPU for small batches and in a compute pass that writes indirect draw arguments for large ones. Static instance bounds are kept until the entity changes, dynamic bounds refresh every frame.

Verification: unit tests for plane extraction and sphere and AABB tests against known frusta in test/view, plus a headless capture showing that objects behind the camera are not drawn. A benchmark on the 100k static scene reports culled versus drawn instances and the frame time with culling on and off.
