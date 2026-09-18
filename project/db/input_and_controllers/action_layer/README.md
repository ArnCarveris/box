# Action layer

Named actions bound to key masks through an InputMap that is authorable from script.

## Details

An action is an entity with an InputAction component and InputMap is a singleton mapping a key mask to a set of actions, written from script as keys.W: { actions.Forward }. A key mask is a u64 with the key in the low 32 bits and eight modifier bits above it for left and right shift, control, alt and super; family masks such as Shift match either side and a modifier key on its own binds as a pure modifier mask. Key names are exported as script constants under keys, including keys.MouseLeft and keys.Ctrl, so bindings read naturally. Any code can emit an action without a key, which is how a UI button and a key binding share one action.

Verification: unit tests cover binding, modifier and family matching, pure modifier bindings, several actions on one mask, rebinding at runtime, unbinding and emitting an action without a key. A script test loads a map from a .flecs file to prove the map is authorable from script.
