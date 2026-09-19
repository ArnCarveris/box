# Checkpoint creation

## Built

`box.checkpoint`: `box_checkpoint_write(world, name, report)` walks every managed
script, records the current value of every component of the entities it owns in an
`ecs_script_edits_t` and writes the resulting source to
`<process folder>/checkpoints/<name>/<script>.flecs`, which goes with the process
folder when the instance stops. The `Checkpoint` singleton redirects `dir` and keeps
`last` and `count`, the `NoCheckpoint` tag keeps engine derived components (
`WorldTransform`, `RenderMesh`, `Terminal`, ...) out of the file, and entities that
no statement declares are reported as `not checkpointed: <path>`. The `checkpoint
[name]` command prints the folder and that report.

## Verified

229 unit tests (`test/checkpoint` 20 new, `test/command` 209), lint 0. The command
scene `test/integration/command/checkpoint.flecs` checkpoints a scene, moves a crate,
checkpoints again and adds an unowned entity; its log and the three checkpoint
folders are `checkpoint.log` and `checkpoint/`. `bench/checkpoint` writes a 5000
entity scene in 295 ms and reports 5000 unowned entities in 0.32 ms.

## Left out

Restore. The write is quadratic in the entities of a script (500 entities 3.2 ms,
5000 entities 295 ms), which is inside the flecs edit API, not this module.
Template body entities are counted, not written, because their statement is shared.
