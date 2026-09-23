# SVG texture publish aborts in readonly mode

A megatower run with any UI panel open aborts when its SVG textures finish, because box_texture_publish calls ecs_modified from a job completion that runs while the world is readonly.

## Details

Reproduce: `box_app --headless --timeout 60 --port <p> <surface.flecs> etc/scenes/megatower.flecs`, then run `fps on` over the command port; within a few seconds the process aborts with `assert(!(world->flags & EcsWorldReadonly))` in entity.c, stack BoxJobUpdate -> box_job_pool_drain -> box_svg_task_complete -> box_svg_publish -> box_texture_publish -> ecs_modified_id -> flecs_commands_flush. The modified event flushes deferred commands (queued by a reactive UI script) while the system is in readonly mode. It reproduces on the tree before the stats work too, so it is not caused by it.

Fix it in `src/texture` or `src/job` (for example publish from an immediate system, or defer the modified through the stage) so SVG textures finishing while a UI script is reactive no longer abort. Verification: a C reproducer in test/render that fails before the fix, the full test/render suite, and the megatower run above with `fps on` and with `stats renderer` staying up for its timeout.
