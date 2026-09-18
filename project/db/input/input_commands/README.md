# Input commands

The device and action layer commands so an agent can drive a running instance from a terminal session.

## Details

Device commands are key.press with an optional duration, key.down, key.up, key.type, mouse.move with a normalised flag, mouse.sweep, mouse.release, mouse.button, mouse.click, mouse.drag, mouse.scroll, input.reset, input.focus and input.wait. Action commands are action.press, action.down, action.up, action.drag and action.list. Commands queue injected events that the OnLoad system consumes over the requested number of frames, mouse.move pins a cursor override until mouse.release, input.reset empties the queue and clears state and input.wait blocks the session until the queue drains. They are flecs script functions under box.command.commands like every other command group.

Verification: unit tests in test/command cover parsing, defaults, the queue and the frame spread of each command, and the focus path where real input is ignored. Command based integration tests type text, click and drag in a scene and keep the command log as the deliverable.
