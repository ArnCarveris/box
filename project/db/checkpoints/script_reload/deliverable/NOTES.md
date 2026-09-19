# ecs_script_reload: approach and limits

Flecs commit `37bcc0b28` (`template_values`).

## What already existed

A managed script (`ecs_script_init`) tags every entity it creates with the pair
`(EcsScript, script)`. `ecs_script_update(world, script, 0, code)` reparses the
script, calls `ecs_script_clear`, which deletes everything holding that pair, and
evaluates the new AST. So added, changed and removed entities were already
reconciled correctly. Two things were missing for a checkpoint restore: no entry
point re-reads the file the script was loaded from, and a failed update wipes the
scene because `ecs_script_clear` runs before the new code is known to be good.

## What was added

`ecs_script_reload(world, script, code)` in `src/addons/script/script.c`, plus
`world::script_reload` in the C++ mixin.

- `code == NULL` reloads from `EcsScript::filename` with `flecs_load_from_file`,
  which is the path a checkpoint rewrites. A script without a filename fails.
- The new source is parsed *before* anything is deleted. The parsed script is
  handed to the existing update path (`flecs_script_update_impl(..., preparse)`
  passes it to `flecs_script_update_parse`), so the source is parsed once, not
  twice. On a parse error the old `EcsScript::script`, `code` and every entity
  stay in place and the message lands in `EcsScript::error`.
- Reconciliation itself is unchanged: clear, then evaluate.

## Limits, in the order they will bite

1. **Only parse failures are atomic.** A script that parses but fails to
   *evaluate* (an unknown component, a division by zero in an expression) still
   clears the scene first and then leaves it empty; `flecs_script_update_error`
   ends with `ecs_delete_with`. Flecs has a quarantined test for exactly this,
   `Reactivity.failed_update_is_atomic` (quarantined 18 Aug 2026). Restore should
   therefore validate a checkpoint by evaluating it in a scratch world, or accept
   that a bad checkpoint empties the scene.
2. **Entity identity is not preserved.** "Changed" means deleted and recreated:
   measured on `foo` in `Reload.changed_entity`, the handle went from 492 to
   4294967788 (same index, new generation). Anything holding an `ecs_entity_t`
   across a restore - selection, a camera target, an editor reference - must
   re-resolve it by name. Preserving identity needs per entity component diffing
   against the old AST, which is a much larger change than this task.
3. **Nothing outside the script is touched.** Entities created by C code or by a
   different script survive a reload, and a checkpoint that wants them back has to
   own them from a script first. This is what makes "entities no script defines
   are not checkpointable" true.
4. **Templates.** Reload refuses a template entity, as update does; reload the
   parent script. Instances are recreated, so with `box.script` they go back to
   pending and refill over the following frames (`Reload.reloaded_instances_are_pending`).
5. **One script at a time.** Restoring several files means several reloads, and
   there is no transaction across them; a file that fails leaves the earlier ones
   already applied. The restore task should reload in a fixed order and report the
   first failure.
