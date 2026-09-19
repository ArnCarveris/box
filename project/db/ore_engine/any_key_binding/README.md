# A binding any key resolves

A key mask that every key and mouse button resolves, so a title screen binds one action and awaits on.pressed instead of scanning the Input singleton or binding a hundred keys.

## Details

Ore Else's title screen starts the game on any key, which the old engine did by walking all 130 slots
of the input singleton in a C system. In box the declarative answer is an action, but a binding needs
a key mask and there is no mask that means all of them, so the port would either bind the 114 key
constants one by one or go back to C. Add a reserved constant under `box.input.keys` (`keys.Any`, and
the families it is worth splitting into) whose bindings resolve on any key or mouse button that went
down this frame, with the key that fired it reaching the handler through the source or the mask of
the `ActionEvent` so a screen can tell which one it was. It is a binding like every other one, so it
sits in a map, obeys the layer order and the consumption of `ore_engine/input_layers`, shows up in
`box_input_bindings` and answers `on.pressed`.

Verification: Unit tests that the action fires for a key, a mouse button and a modifier, that the event carries
the key, that it does not fire on a frame with no press and that a higher layer can consume it, and
the input command scenes unchanged.

## References

- project/db/input/deliverable/README.md
- project/db/ore_engine/input_layers/README.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/game.c
