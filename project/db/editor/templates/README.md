# Template and prefab editing

Selecting an instance child selects its root, Open focuses the instance recursively and child transforms are editable when the template body holds pure values.

## Details

box_editor_resolve walks up from the clicked entity to the nearest prefab or template instance root, unless that root is the opened instance, whose children are then selectable. Open hides the siblings of the instance and of its ancestors, remembering them per level so opening an instance inside an opened instance and closing again restores exactly what was hidden.

For an entity a template body declares, box_editor_source finds its statement through ecs_script_entity_source and scans the value of a component at its own scope depth: numbers, strings, booleans, braces and member keys are literal, an identifier used as a value or an operator makes it computed and the member is shown read only. An edit of a literal value is written back with ecs_script_edits_set and the script reloaded, so every instance follows.

Verified with the Source suite and the instance cases of the Select and Toolbar suites of test/editor.
