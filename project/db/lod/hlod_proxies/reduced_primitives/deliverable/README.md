# Reduced primitive proxies

## What was built
- `box.lod`, the first module of the epic, holding only the proxy generator: no components and no
  renderer records. `box_lod_proxy_build` takes the primitives of a subtree (world transform,
  local bounds, material key, optional filled volume) and returns a few world space `Box`,
  `Sphere` or `Capsule` primitives with their material key, plus the approximation error.
- Clustering is by material key first, so every material keeps at least one shape, then by
  proximity: a volume weighted k means refinement seeded with farthest point sampling, every tie
  broken by input order. The budget is shared by filled volume, and the group that misses the most
  gets another shape while the error is above `max_error`, up to 64 extra shapes.
- The error is the edge of the volume the proxy gets wrong against the union of the inputs, a
  length in world units that `screen_space_error` can project. `box_lod_builder_t` holds the
  working vectors, so a rebuilt subtree allocates nothing.

## Verification
- `tests.json`: 27 tests in `test/lod` covering the cluster split, the budget, the shape choice,
  the error and its scaling, the error bound, material preservation, byte identical repeat builds
  and empty, zero sized, flat, reversed and coincident inputs. `tools/lint.py` reports 0 problems.
- `bench.json`: 1000 primitives build in 69 us, 59 us with eight materials, 204 us with a bound.

## Not in this task
- No proxy records, selection or scene; `hierarchy_traversal` and `screen_space_error` use the
  error later. Shapes are axis aligned, and raising the bound re-clusters every group.
