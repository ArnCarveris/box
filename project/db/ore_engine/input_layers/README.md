# Input layers, consumption and drag

Ordered input maps where a higher layer consumes an action, a drag query from an action, rejection of an action for the frame and a query for whether the UI holds the mouse.

## Details

Ore Else layers its bindings (InputPriority 0 for the game, 1 for the UI), asks whether the place
action is being dragged and from where, rejects an action the game handled so lower layers do not
see it, and asks whether the UI captured the mouse. Give InputMap a priority, resolve maps from the
highest down with an action consumed by the first handler that takes it, add box_input_drag(world,
action, &start) returning whether the action is held past the drag threshold and where it began,
box_input_reject(world, action) for the frame, and box_ui_mouse_captured(world) from the UI hit test
of the frame.

Verification: Unit tests for the order, consumption, drag start and threshold, rejection and capture, the input
command scenes unchanged.

## References

- project/db/input/deliverable/README.md
- project/db/ui/ui_events/deliverable/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/input.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/keybinding.flecs
