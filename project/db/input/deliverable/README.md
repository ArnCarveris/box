# Input: completion report

## What was built
- Device layer: the `Input` singleton with per frame `pressed`, `state` and `current` for 128 keys plus the two mouse buttons at codes 128 and 129, a UTF-8 text buffer, the mouse in window, delta, view centred and normalised coordinates with accumulated scroll, and focus and minimize flags. One OnLoad system clears per frame state, applies the platform events and then the injected ones, drops real input while unfocused or minimized, clears state on focus loss and avoids phantom presses and cursor jumps on refocus. GLFW and HTML5 callbacks queue events through a sink so the system stays the single writer; on the web, keys are left to focused text fields so the inspector console keeps working. Key codes are ASCII where possible, exported as 114 constants under `box.input.keys`.
- Action layer: `InputAction` entities under `actions` with per frame `pressed`, `held`, `released` and `value`; maps bind with pairs `(keys.W, actions.Forward)` or `Binding` children with combined masks such as `keys.Ctrl|keys.S`, since every key constant is also a script variable holding its u64 mask (key code low, eight modifier bits above, families matching either side, pure modifier bindings). `box_input_emit` fires an action without a key and a per frame event list feeds the script events.
- Script events: `on.pressed`, `on.down` and `on.up` async script functions that resume a waiting script in the frame the action fires, carrying the source.
- Input commands: the nineteen device and action commands of the brief as a command group, with cursor pinning, timed injection over frames and `input.wait`.

## Verification
- 842 unit tests across twelve projects, all passing; `test/input` alone holds 63 and `test/command` 198 with the input commands; `test/input` holds 63 tests over `Input`, `Focus`, `Inject`, `Codes`, `Actions` and `Events`, and `test/command` gained the `InputCommands` suite with the `input` command scene.
- Every visual scene matches its baseline and every command scene passes (`integration/`).
- Benchmarks in `bench/` against the culling epic: `Dispatch.help` 122% slower because the command count grew from 25 to 44 and `ls_scope` 23% slower from escaping dots per child; `Input.update_1000_events` 21% slower with the action resolution now running after it; everything else within noise; the device update costs 1.7 us per 1000 events and resolving 100 bindings 2.1 us.
- Wasm image under `wasm`, open it through the status page Inspect link of this epic.

## Lessons
- Registering key callbacks on the window and reporting them handled swallowed typing in the inspector; the engine now defers to any focused text field.
- Making every key constant a script variable is what lets a map combine masks with `|` in plain script, and keeping bindings as pairs or children keeps the map editable from the terminal.
- flecs script async functions resolve a future from a per frame event list without any polling in script.

## Not in this epic
- No real window run was part of the verification; the platform callbacks are covered by the mapping tables and the injection path.
- Gamepads and touch are out of scope of the brief.
