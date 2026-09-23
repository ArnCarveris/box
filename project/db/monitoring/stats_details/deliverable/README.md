# Deliverable

- `tests.json`: render Counters, monitor StatsPanels and command Stats, all passing.
- `bench.json`: bench/render Counters, 2000 blended shapes (about 1840 draw calls a frame):
  counting off, counting on (+1.3%) and every detail on (+2.9%). The closed path was also
  compared with the tree before the change and showed no measurable cost.
- Verification on the default scene: live buffers and textures equal the counts of
  wgpuGenerateReport, instances equal the culling count of the renderer, triangles equal the
  indices of the drawn meshes, texture memory equals the sum of the created textures, and
  every per pass, pipeline, slot and usage table adds up to the frame totals.
