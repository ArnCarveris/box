# Command interface: completion report

## What was built
- `box.command`: `Terminal` sessions with a folder under `/tmp/box/<pid>/<session>` (a `resources` subfolder and `commands.log`), `TerminalInput` runs a line through an OnSet observer, `TerminalOutput` holds the result and an error flag. Commands are flecs script functions under `box.command.commands` registered with `box_command_register`; the dispatcher converts string, i32, f64 and entity arguments (entities relative to the session cwd) and prepends the session entity. A result starting with `error: ` sets the error flag.
- HTTP: `HttpServer` singleton started from `App.port` (`--port`, default 27750, 0 disables), dequeued once per frame in OnLoad. Endpoints `/terminal/new`, `/terminal/close?session=`, `/terminal/exec?session=&cmd=` (GET or POST body, 400 on a command error, 404 for an unknown session) and `/resource/<path>?session=` restricted to the session resources folder (403 on traversal).
- Commands: `help`, `commands [count]`; shell `cd ls touch rm mv add remove set get clear has man` with `.` separated entity paths; search `query iter next done grep grep_recur` with a 100 line cap; `logs [level] [count]` from a mutex guarded ring behind the flecs log hook that also writes `log.txt`; `script_read script_write script_grep script_patch` over loaded scripts with an in memory unified diff helper; `capture [cam0,cam1,...]` through a `Capture` component on cameras that extract consumes for one frame.
- `CommandScript` replays a command file into a session and `Terminal.log` copies the session log to a stable path; `tools/integration.py command` runs `test/integration/command/*.flecs` headless and diffs the logs against `.expected` files.
- `bench/command` microbenchmarks (dispatch, `ls` over 1k children, path lookup, `set` with expression parsing).

## Verification
- 267 unit tests across test/core (25), test/transform (46), test/geometry (32), test/render (37) and test/command (127), see `tests.json` and `tests_<project>.json`.
- Integration: three command scenes (`integration/shell.log`, `search.log`, `script.log`, all matching their expected logs), the visual scene `integration/primitives.png`, and the two stress benchmarks at mean 1.2 to 1.3 ms and p99 2.5 to 2.8 ms for 110k entities (`integration/*_100k.json`).
- Microbenchmarks: `bench/<project>.json` for the four bench projects, compared against the baseline from `project_tool/bench` (see "Baseline" below).
- Wasm build under `wasm`, open `box_app.html#etc/scenes/primitives.flecs`. The command server is native only; the wasm build compiles the module without listening.

## Baseline
`tools/bench.py --baseline project/db/project_tool/bench/deliverable` reports `Extract.static_1000_changed` 6 to 11% slower and `Dispatch.help` 6% slower. The `help` change is real and expected: the baseline listed 17 commands, this epic registers 25. The extract change is machine load: rebuilding the baseline render sources while the other agents were running reproduced the same 7.5%, so the two numbers must be compared again on a quiet machine before the next epic; the only render change since the baseline is the shared slot ownership check, now inlined.

## Lessons
- flecs script functions cannot fail, so failure is a convention on the result string (`error: ` prefix) that the dispatcher strips and turns into the error flag and HTTP status 400.
- `ecs_script_update` stores the new source before parsing, so a failed write or patch must restore the previous source or later patches compound on broken text.
- Systems on a stage must remove components through `it->stage`; `it->world` is read only inside a system and `ecs_remove` asserts.
- The wasm target compiles with strict C11: POSIX functions such as `realpath` need `_XOPEN_SOURCE` before the first include.
- Deleting an entity before its first extract released a slot it never owned; the ownership check is now shared by extract and the remove observer.

## Not in this epic
- The in-game backtick console (needs input and UI), the `render [entity|prefab|template]` command (needs views), checkpoints and `restore` (need a flecs `ecs_script_reload` API), and the device and action input commands (need the input module).
- A `next` after another command ends the iteration; the dispatcher does not yet expose command ordering to command groups.
