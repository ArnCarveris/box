# town.flecs aborts with entity still dirty after extract

## Found
Tracing the dirty list by entity id showed `base` in the list, extracted by the loop and cleared,
and dirty again at the check with the list holding every instance once more: a `(Layer, x)` pair
landing on a prefab during the passes that follow the loop (the view extraction, which is where
the town's incremental templates flush) runs `box_view_layer_on_change`, which requeues every
instance with `box_render_mark_all`. Nothing swept the list again before `box_render_validate`.

## Built
The dirty loop of `box_render_extract` is `box_extract_dirty` and runs twice: before the late
passes as before, and again after `box_render_lod_capture`, so an instance marked late is
extracted in its own frame and the check at the end holds. The second sweep is a no op when
the list is empty.

## Verified
`etc/scenes/town.flecs` runs 200 frames headless in a debug build without the abort (it died in
the first frames before). No unit test reproduces the trigger: the late mark comes from a
deferred flush inside the extract that a test cannot schedule without a hook; `test/render`
green. `bench: false`.
