# Entity shell and component commands

cd, ls, touch, rm, mv, add, remove, set, get, clear, has and man commands operating on the session's current path.

## Details

Paths are entity paths with `.` separators, absolute when they start with `.`, `..` for the parent and `/` or an empty argument for the root; relative paths resolve against `Terminal.cwd`. `cd [path]` changes the cwd (must exist), `ls [path]` lists the children of the cwd or path, one per line, with the component ids of each child after a tab so agents can read structure and types in one call. `touch path` creates the entity, `rm path` deletes it, `mv from to` renames or reparents it.

`add entity id` and `remove entity id` accept a component or pair written as `Comp` or `(Rel, Tgt)` resolved with `ecs_id_from_str`. `set entity component value` parses the value with `ecs_expr_run` against the component type and assigns it, `get entity component` prints the value with `ecs_ptr_to_expr`, `get entity` prints every component with reflection, `clear entity` removes all components. `has entity id` prints `true` or `false`, and when the argument is not an id it is treated as a query expression and tested with `ecs_query_has`.

`man path` prints the `flecs.doc` brief, detail and link components of the entity, or those of the command when given a command name. All output is one value per line with no decoration. Tests in `test/command` cover every command including error paths (missing entity, bad value) and the cwd relative resolution.

## References

- `command/terminal` for `box_command_register` and `box_command_run`
- flecs `ecs_id_from_str`, `ecs_expr_run`, `ecs_ptr_to_expr`, `ecs_query_has`, `flecs.doc`
