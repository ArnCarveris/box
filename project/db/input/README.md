# Input

The device and action input layers, with script event and command access so an action can be awaited, injected or typed from a terminal.

## Details

The device layer is an Input singleton with per frame state for 128 keys, the mouse buttons as codes 128 and 129 and a text buffer, filled by one OnLoad system from OS events and injected events so that injected input is indistinguishable from real input. The action layer turns key masks with modifier bits into named action entities through an InputMap that is authorable from script, and actions integrate with the script event framework so a script can await a press. The same actions can be emitted by UI widgets or by a command, which is what lets a button and a key binding share one action.

Verification is unit tests driven by injected events, both on the native backend and on the wasm backend, and command based integration tests that type, click and drag through the input commands. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Input, InputAction, InputMap and the keys constants, plus the device and action command functions.

## References

- design/DESIGN.md, sections Input and Command interface (input commands)
