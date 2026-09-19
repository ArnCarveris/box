# A binding any key resolves

## Built

`keys.Any`, `keys.AnyKey` and `keys.AnyMouse` are three mask only key codes above the family
modifiers. A `Binding` or a map pair that names one resolves on whatever key, keyboard key or
mouse button of that family is down, so a title screen binds one action and awaits
`on.pressed` instead of walking the `Input` singleton. The binding is like every other one: it
sits in a map, resolves at the layer of that map, consumes and is consumed by the layers around
it, carries its modifiers (`keys.Ctrl|keys.Any`) and shows up in `box_input_bindings`. The
`ActionEvent` reports the key that fired rather than the family, so the screen can tell which
key it was and the consume of a layer covers that key; a key that went down this frame wins over
one that is only still held.

## Verified

13 new tests in `input.AnyKey`, 98 in `test/input`, all green; lint 0 for the files of this task.
The `input` and `input_layers` command scenes pass unchanged. `bench/input` is flat against the
input_layers epic: `resolve_100_bindings` 2.56 us against 2.73 us, the four layer case 3.79 us
against 3.91 us.

## Left out

The any families are masks, not slots, so nothing injects them as a key; `--help` of the key
commands still lists the physical keys only.

## Note

`Codes.spare_codes_are_unique_and_fit_the_table` counts the constants of the keys scope. It went
from 114 to 117 with the upper bound of the codes; no behavioral assertion of it changed.
