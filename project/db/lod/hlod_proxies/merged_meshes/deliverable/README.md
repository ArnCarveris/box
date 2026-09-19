# Merged and simplified meshes

## What was built
- `NOTES.md` evaluates meshoptimizer: MIT, 16 files and a 170 KB static library, an absolute error
  bound and an absolute measured error in its simplify call, attribute support available but not
  needed yet. It fits, so it is the bake3 bundle `meshoptimizer` pinned to `v0.25`.
- `box_lod_merge` takes a subtree the way the proxy builder does, a world transform and a material
  key per source, with the arrays of a `Mesh3` or of a cached primitive mesh in place of the
  bounds. It orders the sources by material key, transforms every vertex into world space, welds
  the repeats and returns one vertex buffer, one index buffer, an index range per material and the
  world bounds. A source without normals gets them from its triangles, and `weld` joins the
  vertices that share a position, which is what lets a simplification cross a seam.
- `box_lod_simplify` collapses that mesh in place, one group at a time so a triangle never takes
  the material of its neighbour, under a ratio and an error bound in world units, drops the
  vertices the survivors no longer use and reports the error it cost in world units.

## Verification
- `tests.json`: 26 tests, `Merge` 16 and `Simplify` 10, over world space merging, material
  grouping, welding, derived normals, defaults, bounds, the ratio, the error bound, its scaling
  with the mesh, compaction, determinism and degenerate inputs. `tools/lint.py` reports 0 problems
  and all 78 tests of `test/lod` pass.
- `bench.json`: a 100k triangle subtree of 125 meshes merges in 5.3 ms, 3.9 ms welded, and
  simplifies to a quarter of its triangles in 9.0 ms.

## Not in this task
- Nothing calls the merge yet: no cache, no proxy record, no upload and no scene. Attribute aware
  simplification, meshlets and `meshopt_setAllocator` are left for later, and welding is exact.
