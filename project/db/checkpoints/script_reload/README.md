# ecs_script_reload in flecs

A reload entry point in the flecs repository so a checkpoint can be restored into a running world.

## Details

Restoring a checkpoint means replacing the content of loaded scripts with the checkpoint files and re-running them, which needs an entry point that updates a script from a path and reconciles the entities it owns, including entities the new version no longer defines. Implement it in the flecs repository at ../../flecs with its own tests and its own commit, then use it here. Keep a short research note on the approach as a deliverable so the next agent does not rediscover it.

Verification: tests in the flecs repository cover reload with added, changed and removed entities and with a script that fails to parse. A test here reloads a script and checks the world matches the new file.
