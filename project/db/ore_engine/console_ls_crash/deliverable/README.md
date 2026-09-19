# Console ls crash

## Built

`ecs_script_init` builds an entity path from an absolute `.filename` when no
`.entity` is given, so loading the console script left an orphan root entity
named `""`. `box_shell_ls`'s `qsort` sorted it, and `box_path_escape("")`
returned NULL, crashing `strcmp`. Fixed `box_ui_console_load`
(`src/ui/console.c`) to pass an explicit `.entity`, and `box_path_escape`
(`src/command/path.c`) to return `""` instead of NULL.

## Verified

Reproduced under `lldb` on debug `box_app`. `Shell.ls_root_entity_with_empty_name`
and scene `console_ls.flecs` fail before the fix, pass after;
`test/command:Shell` 50/50, `test/ui:Console` 11/11, every command scene
green, `tools/lint.py` 0 problems.

## Left out

`src/core/args.c` has the same unsafe absolute-path load, outside ownership;
`box_path_escape`'s fix keeps `ls` safe regardless.
