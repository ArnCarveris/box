# Entity paths through flecs lookup

Command paths cannot name entities by raw id (#123) or with escaped dots; resolve paths and component ids with the flecs lookup functions instead of a custom splitter.

## Details

`ls` prints `primitives.flecs` for the script entity but `cat primitives.flecs`, `cat "primitives.flecs"` and `cat primitives\.flecs` all fail, and `#123` is not accepted anywhere an entity is expected. The path helper splits on dots itself; it must instead hand the string to `ecs_lookup_path_w_sep` relative to the session cwd with the `.` separator, which handles `\.` escapes, `#id` raw ids and root prefixes, with the same fallbacks for `/` and `..` the shell documents. Component and pair arguments keep going through `ecs_id_from_str` and the symbol fallback. `ls` prints names with the dot escaped so its output can be pasted back into a command.

Verification: shell tests for `#id`, an escaped dot in a name, a quoted escaped name, a child of a dotted name, and the round trip from `ls` output to `cat`; the command integration scenes still match.

## References

- `src/command/path.c`, `src/command/shell.c`
- flecs `ecs_lookup_path_w_sep`, `ecs_get_path_w_sep`
