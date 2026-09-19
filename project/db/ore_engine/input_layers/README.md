# Input layers, consumption and drag

Priority ordered input maps, consumption that a binding declares or a handler takes, a drag that is
pushed at a handler instead of polled, and a query for whether the UI holds the mouse.

## Details

Ore Else layers its bindings (InputPriority 0 for the game, 1 for the UI), asks whether the place
action is being dragged and from where, rejects an action the game handled so lower layers do not
see it, and asks whether the UI captured the mouse. Box does the same work without the shape of the
old engine.

Give InputMap a priority and resolve the maps from the highest down. Consumption comes from the
handler the way stopPropagation and Handled do, never from the framework guessing: a binding (or its
map) declares `consume: true` and its key and its action are kept from every map below it before a
handler runs at all, and a handler that decides it took an interaction says so. A shared record on
the world names the raise a handler is running for and carries a `consumed` flag: `box_event_stop`
(script `event.stop()`) sets it for a handler that runs inside the raise, and
`box_input_consume(world, action)` (script `input.consume(action)`) takes a whole action for the
rest of the frame, which is what an async script task that resumes after the delivery uses. Either
way the key is taken from the maps below and the actions they bound to it are cleared for the frame.
One rule orders a frame: the highest map first, then the deepest UI node over the scene, then the
scene itself.

Drag stays the push model box already has. `on.dragged(action)` resumes a task in every frame a
bound action is held past `BOX_INPUT_DRAG_THRESHOLD`, carrying where the drag began and where the
cursor is now, so a drag painting follows the cursor without a polling call. There is no
`box_input_drag` and no `box_input_reject`.

`box_ui_mouse_captured(world)` answers from a `UiCapture` singleton the UI hit test writes every
frame, so a script reads the same answer as data.

Verification: Unit tests for the order, the declared consume, the handler consume and its cascade,
the drag threshold and its raise, and the capture; the input command scenes unchanged and a command
scene of this task showing a higher map keeping a key.

## References

- project/db/input/deliverable/README.md
- project/db/ui/ui_events/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/input.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/keybinding.flecs
