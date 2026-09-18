# Script commands

script_read, script_write, script_grep and script_patch over the scripts loaded into the instance.

## Details

Scripts loaded with `ecs_script` are entities with the `EcsScript` component that keep their filename and source. `script_read path` prints the source of the script whose filename matches the argument (exact, or basename when unique); `script_write path code` replaces the source with `ecs_script_update` and prints `ok` or the parser error; `script_grep text` prints `file:line:text` for matching lines across all loaded scripts.

`script_patch file patch` applies a minimal unified diff (only `@@`, ` `, `+` and `-` lines, one or more hunks) to the script source in memory and calls `ecs_script_update` with the result, printing `ok` or the first hunk that failed to apply. The line based patch helper is a standalone function in its own file with its own tests. Sources are kept in the script entity so patches compound.

Tests in `test/command` load a script from a temporary file, read it, patch it, verify that the world reflects the change (an entity gained a component), grep it and check the failure paths (unknown file, hunk mismatch, parse error keeps the old world state).

## References

- `command/terminal`
- flecs `ecs_script_update`, `EcsScript`
