# town.flecs aborts with entity still dirty after extract

Running box_app with etc/scenes/town.flecs aborts in box_render_validate with 'entity base still dirty after extract'; every instance the extract marks dirty must be extracted or cleared in the same frame.

## Details

`bake3 run apps/box_app -- etc/scenes/town.flecs` dies in the first frames:

```
fatal: extract.c: 339: abort(): entity base still dirty after extract (INTERNAL_ERROR)
box_render_validate <- box_render_extract <- BoxRenderExtract
```

`base` is the large ground `Box` of the town at the root of the scene. The dirty loop in
`box_render_extract` (`src/render/extract.c`) walks `state->dirty` and extracts an entity only
when it is alive and has a `RenderMesh`, then clears the list, while `box_render_validate`
walks every `RenderInstance`, so an instance that is in the list without a mesh at that moment
(a `Box` whose mesh is being regenerated, a mesh replaced by the geometry module after the
mark, or an instance left behind when its mesh was removed) keeps `dirty` set and trips the
check. The other candidates are marks that happen after the loop in the same extract
(`box_render_lod_touch` from `box_render_mark_dirty`, emitters, the LOD capture) and the
debug bake path in `extract_bake.c` marking everything. Reproduce with the town scene under
a debug build, break on the abort, find who marked `base` and why it was skipped, and fix the
extract so a skipped entity is either cleared or kept in the list for the next frame, never
left dirty; add a `test/render` case with the shape found (a static box whose mesh is
replaced in the frame it is marked, or whatever the trace shows).

Verification: the reproducer fails before the fix and passes after, the town scene runs by
hand for a few seconds in debug without an abort, `test/render` green, every visual scene
unchanged.

## References

- `src/render/extract.c`, `src/render/instance.c`, `src/render/extract_bake.c`,
  `src/render/extract_lod.c`, `apps/box_app/etc/scenes/town.flecs`
