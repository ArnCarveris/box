# Hover present on the first frame

A cursor already over a target on frame 0 picks before the template's async on.hover waiter registers and the event is lost; deliver the current hover to a late waiter.

## Details

`BoxPickingUpdate` runs in OnLoad and raises hover once when the hit changes. A template instantiated in the same frame registers its `on.hover` future after that, so a scene that starts with the cursor on a target never sees the event, and the picking cache never re-fires because nothing changed. The hover scene had to sweep the cursor in to work around it. Make a waiter that registers while `Hovered` is already on its entity (or on an entity it bubbles from) resolve on the next update, the same way `on.pressed` in the input module treats a key already down, and keep click and drag single shot.

Verification: a reproducer in `test/picking` that instantiates a template with an `on.hover` handler while the cursor already rests on it and expects the handler to run, failing before the fix; the command test and the hover scene unchanged.

## References

- `project/db/picking/hover_scene/deliverable/README.md`, `src/picking/`, `src/input/` (futures)
