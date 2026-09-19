# ecs_script_reload in flecs

## Built

`ecs_script_reload(world, script, code)` in flecs (`37bcc0b28`, branch
`template_values`), with a `world::script_reload` C++ mixin. `code == NULL` re-reads
`EcsScript::filename`, which is the path a checkpoint rewrites. The new source is
parsed before the old entities are cleared and the parsed script is handed to the
existing update path, so a parse failure leaves the world and the loaded script
untouched and only fills `EcsScript::error`. Reconciliation is the existing
clear-and-evaluate: removed entities are deleted, changed ones updated, added ones
created.

## Verified

Flecs: a new `Reload` suite in `test/script`, 12 cases (added, changed and removed
entity, removed component, parse error from code and from file, reload from file,
no filename, not a script, entities outside the script). Whole `test/script`
project: 8034 pass, 1 pre-existing unrelated failure
(`TemplateInheritance.derived_prop_override_struct_default`, a float formatting
assert that fails on the untouched tree too). Box: a `Reload` suite in
`test/script`, 3 cases that write a scene file, load it through `box.script`,
rewrite it and reload; `test/script` 21 pass. Lint 0.

## Left out

Only parse failures are atomic; an evaluation failure still empties the scene, and
entity handles change across a reload. Both, and what restore should do about them,
are in `NOTES.md`. No bench: reload runs on a checkpoint restore, not per frame.
