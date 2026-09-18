# Action layer

Actions are entities under the root `actions` scope with an `InputAction`: a description plus the
`pressed`, `held`, `released` and `value` the engine writes every frame. An entity with `InputMap`
is a map (`box.input.map` by default) that binds a key with a pair or with a `Binding` child:

    actions { Forward { InputAction: {description: "Move forward"} } }
    input_map { InputMap  (keys.W, actions.Forward)
      save { Binding: {mask: keys.Ctrl|keys.S, action: actions.Save} } }

A key mask is a u64: the key code in the low 32 bits, bits 32 to 39 for left and right shift, ctrl,
alt and super; `keys.Shift`, `keys.Ctrl`, `keys.Alt` and `keys.Super` match either side. Every
`keys` constant is also a script variable holding its mask, which is what `keys.Ctrl|keys.S`
combines, and a modifier key's mask is its bit, so `keys.ShiftLeft` alone binds a pure modifier.
`BoxInputActions` runs in OnLoad after `BoxInputUpdate` and fires, per key, the matching bindings
with the most modifier families, plus a pure modifier binding whose family no key binding claims.
`box_input_emit(world, action, source)` fires an action for one frame without a key and
`box_input_events` returns the private `ActionEvents` list of the frame. 56 tests pass in
`test/input`, 16 of them in `Actions`, and `Actions.resolve_100_bindings` costs 2.09 us.
