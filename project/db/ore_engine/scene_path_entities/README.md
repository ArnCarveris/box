# Scenes loaded by absolute path create an empty root

Loading a scene by absolute path without a script entity builds an entity hierarchy from the path, whose leading slash leaves a root entity with an empty name in every world; load scenes on an explicit entity.

## Details

`console_ls_crash` found that `ecs_script` with a filename and no `.entity` derives the script
entity from the absolute path, so the leading `/` creates a root entity named `""`, which is
what crashed `ls`. `src/core/args.c` loads the scenes named on the command line the same way,
so every `box_app` world still carries the orphan root and any command that walks the root
sees it. Load each scene on an explicit entity under a `scenes` scope named after the file's
stem, so `ls` at the root lists the scene as a proper entity and a reload replaces it, and
check every other `ecs_script` call in `src` for the same shape.

Verification: a `test/core` case that loads a scene by absolute path and asserts no root entity
has an empty name and the scene entity exists under `scenes`, `test/core` and `test/command`
green, every command scene green.

## References

- `src/core/args.c`, `src/ui/console.c`, `project/db/ore_engine/console_ls_crash/deliverable/README.md`
