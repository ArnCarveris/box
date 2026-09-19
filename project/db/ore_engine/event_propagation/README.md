# One propagation model for input, picking and UI

box.picking and box.ui adopt the shared Raise record and event.stop of the input layers, so one rule orders delivery: higher priority map, deepest UI node, then the scene bubble.

## Details

The input layers task introduced `Raise {target, source, priority, consumed}`, `box_event_stop`, `event.stop()` and `input.consume`, but the picking bubble in `src/picking/events.c` and the UI hit test still consume implicitly and stop at the first handler. Make both read and write the same record: a UI hit raises through the UI nodes from the deepest up, a handler that calls `event.stop()` (from C, inside delivery) or `input.consume` (from a script task, after its await resumes) ends the bubble and keeps the pick from the scene, an unconsumed UI event falls through to the scene ray, and the scene bubble walks the parent chain the same way; `event.consumed()` reports the state to a later handler. Keep the existing behaviour where no handler consumes, so every command scene and the picking tests still pass, and document the order in `include/box/input.h`.

Verification: unit tests in `test/picking` and `test/ui` for stop inside the bubble, fall through from UI to scene, and consume from a script task; every command and visual scene unchanged.

## References

- `project/db/ore_engine/input_layers/deliverable/README.md`, `src/picking/events.c`, `src/ui/hit.c`, `src/ui/events.c`
