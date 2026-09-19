# Key waiters registered at scene load

An on.pressed await registered while the scene loads is lost in command scenes; deliver the first key to a waiter that registers in the frame the scene loads, like the hover catch-up in picking.

## Details

The console task found that a template whose async block awaits `on.pressed` at scene load never fires in a command scene: the key events of the first frame are raised before the waiter exists and nothing retries. Find the ordering (script evaluation versus the input update within the load frame), then either defer the first input update until the scripts of the frame have run or keep the last raise per key while no waiter was reached and retry it once when one registers, the way `box_pick_hover` retries a missed hover. Keep click and typed characters single shot.

Verification: a reproducer in `test/input` that registers the waiter and raises the key in the same frame and expects the handler to run, failing before the fix; `test/input` and every command scene unchanged.

## References

- `project/db/ui/console/deliverable/README.md`, `project/db/picking/hover_race/deliverable/README.md`, `src/input/`
