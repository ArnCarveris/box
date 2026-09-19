# HLOD proxies

The three proxy kinds of the LOD epic, each delivered by its own subtask:

- `reduced_primitives`: `box_lod_proxy_build` clusters a subtree's primitives by material and proximity into a few boxes, spheres and capsules with an approximation error in world units.
- `merged_meshes`: `box_lod_merge` and `box_lod_simplify` merge a subtree per material group and simplify it with meshoptimizer under an error bound.
- `impostors`: eight views of a subtree captured into an atlas and drawn as a billboard picking the nearest tile, the last level before the bounds box.

`tests.json` and `bench.json` are the `test/lod` and `bench/lod` reports covering all three; the traversal picks the cheapest kind under a view's threshold through the cache of `cache_and_jobs`.
