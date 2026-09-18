# Log capture and log commands

A ring buffer capturing flecs log output so logs [level] [count] and commands [count] return recent entries.

## Details

The module installs a log hook through `ecs_os_api.log_` that forwards to the previous hook and appends each message with its level to a ring buffer of 1024 entries owned by a private `LogBuffer` singleton (stored as a heap struct). The hook must be thread safe with a spinlock since the renderer thread may log. The buffer also writes every line to `<process_dir>/log.txt`.

`logs level count` prints the last count entries at the given level or more severe, where level is one of `error`, `warning`, `info`, `debug` (or the flecs numeric level), oldest first, as `level\tmessage`; both arguments are optional with defaults `warning` and 50. `commands count` in the terminal task reads the per session command log; this task moves it to share the same ring buffer implementation so both use one helper.

Tests in `test/command` emit `ecs_err`, `ecs_warn` and `ecs_trace` at a raised log level and check filtering, ordering, the count cap and the wrap around of the ring.

## References

- `command/terminal`
- flecs `ecs_os_api.log_`, `ecs_log_set_level`
