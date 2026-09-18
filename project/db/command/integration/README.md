# Command based integration tests

A CommandScript component that runs a command file against a terminal, and an integration.py mode that collects the command logs.

## Details

`CommandScript` is a component on a terminal entity with a `path` to a `.cmd` file; an OnSet hook reads the file and runs one command per line through `TerminalInput`, skipping blank lines and `#` comments, so the session's `commands.log` holds every command and result. Scenes under `test/integration/command/<name>.flecs` set up content and a terminal with a `CommandScript` pointing at `<name>.cmd` next to them.

`tools/integration.py command` runs each scene headless for a few frames with `--port 0`, copies the session `commands.log` to `test/integration/out/<name>.log`, and compares it with `<name>.expected` when that file exists, reporting pass or fail. Add three scenes: a shell walk (ls, cd, get, set, has), a search scene (query, iter, next, grep_recur) and a script scene (script_read, script_patch, ls showing the effect).

The epic deliverable collects these logs, the JSON test reports of every test project and the visual capture screenshot. Update `README.md` and `docs/ARCHITECTURE.md` with the command interface and the new test kind.

## References

- `tools/integration.py`
- every other task in the `command` epic
