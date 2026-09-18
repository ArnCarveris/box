# Terminal sessions and command dispatch

The box.command module: Terminal session entities with per session folders, a line dispatcher that calls script functions, and per session command logs.

## Details

`include/box/command.h` declares `Terminal` (cwd path string, session dir string written by the engine), `TerminalInput` (line string) and `TerminalOutput` (result string, error flag) with reflection. An OnAdd hook on `Terminal` creates `<App.process_dir>/<entity name>` and its `resources` subfolder and opens `commands.log`; OnRemove closes the log and removes the folder. The `terminals` scope entity is created by the module so `terminals.agent0 { Terminal: {} }` works from script.

An OnSet observer on `TerminalInput` splits the line into a command name and whitespace separated arguments (double quotes group a single argument), looks up the function entity under `box.command.commands`, converts each argument to the parameter type declared on the function (string, i32, f64 or entity, entities resolved relative to the session cwd) and calls `ecs_function_call` with the session entity prepended as the first argument. The result string becomes `TerminalOutput`; an unknown command, wrong argument count or conversion failure produces a one line error and sets the error flag. Commands are registered from C with a helper `box_command_register` that fills an `ecs_function_desc_t` with the session parameter first and takes a one line description used by the `help` command, which lists all commands with their parameters.

This task ships `help` and a `box_command_run` internal helper (set input, return output) used by tests and later tasks, plus the `commands [count]` log command reading from the per session log ring. Tests in a new `test/command` project cover folder lifecycle, parsing with quotes, unknown commands, argument conversion, help output and the log. Commands must not require public native functions beyond module import.

## References

- `design/DESIGN.md`, "Command interface"
- flecs `include/flecs/addons/script.h`, `ecs_function_init` and `ecs_function_call`
