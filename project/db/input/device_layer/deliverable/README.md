# Device input layer

`box.input` holds `Input`: 130 `KeyState {pressed, state, current}` slots (128 key codes plus the
mouse buttons), a 32 byte UTF-8 text buffer with its length, a `Mouse` with window, delta, view,
normalized, scroll and scroll_delta, and `focused` and `minimized`. A private `InputState` keeps
two `InputEvent` vectors, one the platform layer pushes into through a sink callback and one
injection pushes into with `box_input_inject`, next to `box_input_reset` and `box_input_pending`.

`BoxInputUpdate` runs in OnLoad after the surface poll: it clears the per frame fields, applies
the platform events, then the injected ones. Real input is dropped while the window is unfocused
or minimized, focus changes clear the key state and invalidate the cursor so refocusing neither
presses a key nor jumps the mouse. An injected event carries `delay` in frames and a down carries
`duration`, which schedules its up that many frames later, so a press lasts one frame by default
and a sweep spreads moves over frames. `BoxInputBind` binds the sink and the cursor capture.

Key codes are ASCII where possible (`W` 87, Space 32, Enter 13, Delete 127); arrows, modifiers and
navigation take the spare lowercase range, F1 to F12 take 14 to 25, the keypad takes the remaining
gaps, the mouse buttons are 128 and 129 and the four family modifiers at 130 to 133 are reserved
for the action layer masks. All 114 names are script constants under `box.input.keys`, and
`platform_glfw.c` and `platform_web.c` map their native codes with `box_platform_key_from_native`,
where web adds pointer lock and page visibility.

40 tests pass in `test/input`, `Input.update_1000_events` costs 1.66 us, test/core and the other
test projects are green, the wasm build links and the lint reports 0 problems.
