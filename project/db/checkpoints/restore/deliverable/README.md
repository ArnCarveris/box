# Checkpoint restore

## Built

`box_checkpoint_restore(world, name, keep, report)` and the `restore [name]`
command reload every managed script whose file the checkpoint folder holds through
`ecs_script_reload`; without a name the folder is `Checkpoint.last`. Every file is
parsed before the first reload, so a checkpoint that does not parse leaves the
world as it was and a file that fails to evaluate stops the restore and is named
after `restore failed: `. Entities no script owns survive and are listed as
`not restored: <path>`, the script that declares the session is kept so a restore
cannot delete the terminal under it, and the `Checkpoint` folder and count are put
back after the reloads.

## Verified

249 tests (`test/checkpoint` 40, 20 new: `Restore` covers both forms, an unknown
name, no checkpoint, a corrupt file, added, deleted and unowned entities, changed
handles and the kept script; `Survive` covers renderer slots, the collider, a
template collision root and a controller following the recreated entities).
`test/command` 209 and `test/script` 21 unchanged, lint 0. `checkpoint_restore.log`
is the session log of the command scene, which checkpoints, moves a crate, deletes
an entity, adds one no script owns and restores twice. `bench/checkpoint` restores
a 500 entity scene in 4.17 ms.

## Left out

The quadratic write (`checkpoints/edit_perf`). Handles change across a restore,
which nothing preserves, and a restore is atomic up to the first bad file.
