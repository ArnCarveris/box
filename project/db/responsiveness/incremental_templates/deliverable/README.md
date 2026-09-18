# Incremental template loading

`box.script` marks every template manual: an OnSet observer on `EcsScript` adds
`EcsScriptTemplateManual` where `template_` is set (`yield_existing` also covers earlier
templates), so the addon leaves each instance with `(EcsScriptTemplatePending, template)`.
`BoxScriptUpdate`, an immediate PreUpdate system, calls `ecs_script_template_update` on
the instances a frame affords; non template entities appear with the script.

The budget is its own `ScriptSettings` singleton, not `JobSettings`: this runs on the main
thread in a system instead of the job pool, so a scene tunes template fill rate and job
completions apart. `templates_per_frame` 256, `budget_ms` 1, `reorder_distance` 4,
`reorder_interval` 30, `nearest_first` true, `enabled` false expands everything in one
frame; `ScriptStats` reports `pending`, `instantiated` and `worst_frame_ms`.

An OnAdd observer on the pending pair only sets a dirty bit. A rebuild scans the uncached
query of pending instances, keys each entry with the squared distance from its
`WorldTransform` (else `Position`) to the active camera and heapifies in O(n); a pop is
O(log n). It runs when the queue is dirty, the camera moved past `reorder_distance` or
`reorder_interval` frames passed: 0.63 ms per 100k reorder, 89k instances/s expanded.

`templates_100k.flecs` (100k instances, 200k renderables), 300 frames: mean 1.08 ms, p99
1.83 ms, worst 2.33 ms; complete at frame 3933, worst 47.8 ms there from the renderer
rebuilding its static shadow cascade every frame while the set grows. The same scene with
`enabled: false` spends 4126 ms in one frame. 18 tests pass.
