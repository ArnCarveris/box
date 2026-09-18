# In game console

The backtick console, an overlay terminal backed by the same sessions agents use over HTTP.

## Details

Pressing backtick opens an overlay that creates a terminal session, sends the typed line as TerminalInput and shows TerminalOutput in a scrollback, with history, completion of command names and entity paths and a log view. The overlay captures keyboard input while open and releases it when closed, following the focus rules of the device layer. It is built from the UI components and script, which is also the proof that the UI framework can carry a real widget.

Verification: unit tests cover opening and closing, input capture, history, completion and scrollback. A command based integration test opens the console, runs commands and captures the screen, completing the deferred list from the command epic.
