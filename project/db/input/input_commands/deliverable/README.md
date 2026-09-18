# Input commands

`src/command/input_cmd.c` registers nineteen commands under `box.command.commands`: `key.press`,
`down`, `up` and `type`, `mouse.move`, `sweep`, `release`, `button`, `click`, `drag` and `scroll`,
`input.reset`, `focus` and `wait`, and `action.press`, `down`, `up`, `drag` and `list`. Positions
are `x,y` strings that `norm` reads as normalized, durations and steps are frames, booleans are
`true`, `false`, `1`, `0`, `on` or `off`, and a key is any name of the `keys` scope, case
insensitive. The device commands queue injected events with the delay and duration of the device
layer, so a sweep or a drag spreads over its frames; the dotted names resolve as a path in the
dispatcher and `help` lists them per group.

`mouse.move`, `mouse.sweep` and every drag pin the cursor, so the real pointer stops moving it
until `mouse.release` or `input.reset`. A command file runs inside one frame, so `input.wait`
cannot block: it reports the events still queued (`20 events pending`), returns `queue drained`
when there are none, and the caller repeats it in a later frame. The action commands hold an
action with `box_input_hold`, which the binding resolve of the next frame turns into a press, a
hold of a number of frames or a hold until `action.up`, so they raise the same events as a key.

24 tests pass in `test/command` suite `InputCommands`, `test/integration/command/input.flecs`
types, clicks and drags through the commands, and `InputCommands.key_press_10` costs 16.7 us.
