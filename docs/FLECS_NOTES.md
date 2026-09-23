# Flecs issues met while building the editor

Observations from the editor epic that look like they belong in flecs rather
than in box. Each one has a workaround in box, named after the observation.

## Manual template instance re-marked pending inside its own table move

Adding `Hovered` to an instance of a template that reads `this?[Hovered]` and
is marked `ScriptTemplateManual` re-evaluates the template synchronously from
the OnAdd event. The re-evaluation adds `(ScriptTemplatePending, T)` to the
same entity while flecs is still moving it, and the next command flush fails on
`assert(record->table == dst_table)` in `flecs_move_entity`.

Workaround: box.script no longer marks templates declared inside a module as
manual (`box_script_incremental` in `src/script/script.c`), so the widget and
editor templates are never pending.

## A press delivered to a task that a re-evaluation then discards is lost

When the cursor arrives on a template instance and the button goes down in the
same frame, the hover re-evaluates the template, which restarts its async
blocks. The `on.press` waiter of the old block is still registered when the
press is raised, resolves its future, and the old task is discarded before it
resumes. The future is freed but never reported as cancelled by
`ecs_script_future_is_cancelled`, so a native event source cannot tell a
consumed press from a lost one. The restarted block registers a new waiter one
system later and never sees the press.

Workaround: `BoxPickingUpdate` is an immediate system, so the hover's
re-evaluation and the task restart happen before the press is raised, and a
press that finds no waiter is kept while the button is held and handed to the
first waiter that registers on the pressed entity (`press_missed` in
`src/picking`).

It would help if restarting the blocks of an instance either cancelled the
futures of the old tasks (so a source sees `is_cancelled`) or re-delivered a
resolved value to the restarted block.

## Interpolating an entity into a string yields its path

`"n_{r.entity}"` gives `n_scene.ball` for a child entity, and a name with a dot
is created as a path, so the row ends up as a child of another row. Expected,
but easy to fall into; the editor names rows by a numeric id member instead.

## Modifying ScriptSettings before any script is loaded fails

`ecs_singleton_modified(world, ScriptSettings)` right after importing Box, with
no script loaded yet, fails in `box_script_on_settings` with `ecs_field` on
entity 0 (`flecs_field_shared`). The app loads the builtin scripts first, so it
does not hit this. Not investigated further.

## A managed script cannot refer to `root.other {}` by path

`root.other { Focused }` in a managed script fails with "invalid path in entity
name", while `text.focus(root.other)` resolves the path fine as an expression.
