# Query and grep commands

Named queries with an iteration mode (query, iter, next, done) and grep and grep_recur over names and component values.

## Details

`query name expr` creates a query with `ecs_query(world, {.expr = expr, .entity = <name under session>})` so it can be inspected and reused; a second `query` with the same name replaces it. `iter name` puts the session in iteration mode by storing the query and an `ecs_iter_t` in the session's private state; `next` and `next count` print the next 1 or count matched entities as path and, for each query term, the component value with `ecs_ptr_to_expr`; `done` finishes the iterator. Any other command exits iteration mode first, and `next` without an active iterator is an error.

`grep text` searches the children of the cwd: entity names, component names and component values serialized with `ecs_ptr_to_expr` are matched with `strstr`, printing `path\tcomponent\tvalue` for each hit. `grep component text` restricts the search to one component. `grep_recur` and `grep_recur component text` do the same over all descendants using `ecs_children` recursively, collecting entities first before serializing.

Results are capped at a `limit` of 100 lines with a final `... n more` line so replies stay small. Tests in `test/command` cover query creation, iteration in chunks, exiting iteration mode on another command, grep on names and values, and the recursive variant.

## References

- `command/shell` for path resolution helpers
- flecs `ecs_query_init`, `ecs_query_iter`, `ecs_query_next`, `ecs_ptr_to_expr`
