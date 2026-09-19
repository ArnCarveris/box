# Several waiters on one action in a scene

In a scene loaded from a file, a key raise reaches only the first of two script waiters on one action; resuming one template appears to cancel the others' awaits.

## Details

`Events.several_waiters_resume_on_one_action` passes as a unit test, so the loss is specific to managed scripts: two templates in one scene file each awaiting `on.pressed` of the same action, and only the first runs when the key fires. Reproduce with a command scene (two templates, one key), then find whether resuming the first task re-evaluates or clears the script's pending futures (in `box.input`'s waiter list, or in flecs' `ecs_script_future_resolve` handling of managed scripts, in which case the fix goes to the flecs repository with its own test and a single line commit, never pushed). The game's title, HUD and inventory screens all await the same keys, so this blocks the UI epic.

Verification: the reproducer fails before the fix and passes after, `test/input`, `test/script` and every command scene green.

## References

- `project/db/ore_engine/input_layers/deliverable/README.md`, `src/input/events.c`, `../../flecs/src/addons/script`
