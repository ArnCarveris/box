# Linear script edits in flecs

The flecs script edit API is quadratic in the entity count, 5000 entities take 295 ms to checkpoint; make the edit path linear in the flecs repository so a large scene checkpoints in milliseconds.

## Details

`box_checkpoint_write` rewrites a script through `ecs_script_edits_*` so untouched source survives byte for byte, but every edit walks the whole statement list or re-scans the source, so 500 entities take 3.2 ms and 5000 take 295 ms. Profile the edit path in `../../flecs/src/addons/script`, index statements by entity once per edit session and apply the edits in one pass over the source, with the flecs tests for edits kept green and a flecs benchmark or timing test for 10k statements. Commit in the flecs repository with a single line message and never push.

Verification: the `bench/checkpoint` write cases over 5000 and 50k entities recorded in the deliverable, linear in the entity count, and `test/checkpoint` unchanged.

## References

- `project/db/checkpoints/checkpoint_write/deliverable/README.md`, `src/checkpoint/`, `../../flecs/include/flecs/addons/script.h`
