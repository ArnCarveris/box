# Input layers, consumption and drag

## Built

`InputMap.priority` makes the maps of a world layers that resolve from the highest down.
Consumption is declared or taken, never guessed: `Binding.consume` (or `InputMap.consume` for every
binding of a map) keeps a key and its action from the maps below before a handler runs, and a
handler that took an interaction says so through the `Raise` record of the world -- `box_event_stop`
/ `event.stop()` inside a raise, `box_input_consume(world, action)` / `input.consume(action)` from a
task that resumes after the delivery. Either clears the actions the maps below bound to the same
key for that frame, and an action whose key is still down carries on as held instead of pressing
again. Events are delivered highest map first. Drag stays push: `on.dragged(action)` resumes a task
in every frame a bound action is held past `BOX_INPUT_DRAG_THRESHOLD`, carrying where it began and
where the cursor is now. `box_ui_mouse_captured(world)` answers from the `UiCapture` singleton a
PostLoad system in the UI hit test writes each frame.

## Verified

21 new tests in `input.Layers` and 6 in `ui.Capture`; 85 in `test/input`, 180 in `test/ui` and 41,
35, 216, 21, 40, 34, 89, 147, 386, 157, 83 in the other projects run, all green; lint 0. All 16
command scenes pass, `input_layers.flecs` among them, and all 89 visual scenes stay bit identical.
`Actions.resolve_100_bindings` is 2.66 us against 2.1 us in the input epic (the run shares the
machine; `update_1000_events` drifted 10% too), and the same 100 bindings over four layers cost
3.86 us.

## Left out

`event.stop()` only binds a handler that runs inside the delivery, because a script task resumes
after it; `box.picking` and `box.ui` do not read the `Raise` record yet, so their bubble and their
hit still consume implicitly.
