# meshoptimizer for error bounded simplification

## Decision

Vendor it, as the bake3 bundle `meshoptimizer` pinned to tag `v0.25`. Writing an
edge collapse simplifier with a usable error metric is weeks of work that this
library already does better, and the part the epic needs, an error the screen
space metric can project, is a documented part of its API.

## Licence

MIT, Arseny Kapoulkine, 2016-2025, one `LICENSE.md` with no attribution clause
beyond keeping the notice. The same licence as `stb` and `cgltf`, which the
project already bundles.

## Size and build

- 16 `.cpp` files and one header in `src/`, 13k lines, 500 KB of source; the
  `demo`, `gltf` (gltfpack), `js` and `tools` folders are not built.
- Its `CMakeLists.txt` builds a static `libmeshoptimizer.a` of 170 KB with
  `MESHOPT_BUILD_DEMO=OFF`, `MESHOPT_BUILD_GLTFPACK=OFF` and
  `MESHOPT_BUILD_SHARED_LIBS=OFF`, and installs `meshoptimizer.h` into
  `include/`, which is exactly what a bake3 cmake bundle expects.
- It is C++ without the STL and without exceptions, but its default allocator
  calls `operator new`, so the bundle declares `"lib": ["c++"]`. The project
  already links `-sDEFAULT_TO_CXX=1` on wasm, where `emcmake cmake` configures
  the bundle.
- No dependencies of its own, no threads, no allocation outside the calls.

## The simplify API

`meshopt_simplify(destination, indices, index_count, vertex_positions,
vertex_count, stride, target_index_count, target_error, options, result_error)`
collapses an index buffer against a position buffer it never writes to, and
returns the number of indices it kept. Two goals bound it: it stops at
`target_index_count` and never lets the surface move further than
`target_error`. With `meshopt_SimplifyErrorAbsolute` both `target_error` and
`result_error` are lengths in mesh units, so the error needs no scaling before
`box_lod_error_px` projects it; without the flag they are relative to the extent
of the mesh and `meshopt_simplifyScale` converts. `meshopt_SimplifySparse` says
the indices are a subset of the vertex buffer, which is what makes simplifying
one material group of a merged mesh cost what that group is worth.
`meshopt_SimplifyLockBorder` pins the open boundary, and `meshopt_SimplifyPrune`
drops disconnected parts; neither is used yet.

## Attributes

`meshopt_simplifyWithAttributes` takes up to 32 extra floats per vertex with a
weight each, and a per vertex lock, so normals, uvs and colors can steer the
collapse order. The merge already keeps its attributes in separate arrays, so
feeding it an interleaved attribute buffer is the one copy it would cost. It is
left for the task that finds the current normals too soft, because the weights
need tuning against a real scene.

## Integration cost

One bundle entry, one `#include <meshoptimizer.h>` and about sixty lines in
`src/lod/simplify.c`: a loop over the groups, the absolute error options and a
compaction of the vertices the result no longer uses. Nothing in the library
allocates through flecs; `meshopt_setAllocator` can route it through
`ecs_os_malloc` later if the job system wants that.

## What was not evaluated

The clusterizer (`meshopt_buildMeshlets`) and the nanite style
`meshopt_partitionClusters`, which the epic may want when proxies become
hierarchical, the vertex and index codecs, and the quantization helpers.
