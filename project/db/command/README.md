# Command interface

Terminal sessions, HTTP endpoints and script-function commands for driving a running instance from agents.

## Details

Agents drive a running instance through terminal sessions. A session is an entity with a `Terminal` component; creating one makes a folder `/tmp/box/<pid>/<session>` with a `resources` subfolder and a `commands.log`, and deleting it removes the folder. A command line is executed by setting `TerminalInput` on the session; an OnSet hook parses the line, looks up the command as a flecs script function under `box.command.commands`, calls it with the session entity and the string arguments and stores the concise text result in `TerminalOutput`. Every command and result is appended to the session log.

The HTTP layer is thin glue over those components: `/terminal/new` creates a session entity, `/terminal/close?session=` deletes it, `/terminal/exec?session=&cmd=` sets `TerminalInput` and replies with `TerminalOutput`, and `/resource/<path>` returns files from the session resources folder only. The server is an `HttpServer` singleton whose port comes from `--port`; requests are dequeued once per frame in OnLoad so handlers run on the main thread with full world access. Command groups live in separate files: shell (cd, ls, touch, rm, mv, add, remove, set, get, clear, has, man), search (query, iter, next, done, grep, grep_recur), logs (commands, logs), script (script_read, script_write, script_grep, script_patch) and capture.

Command based integration tests are scenes under `test/integration/command` that add a terminal with a `CommandScript` pointing at a `.cmd` file; `tools/integration.py command` runs them headless and collects the command logs as deliverables. The in-game backtick console, the `render` command, checkpoints and input commands wait for the UI, view, checkpoint and input epics.

## References

- `design/DESIGN.md`, sections "Agent based development" and "Command interface"
- flecs `include/flecs/addons/script.h` (`ecs_function_init`, `ecs_function_call`) and `http.h`
