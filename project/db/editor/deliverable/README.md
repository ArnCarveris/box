# Scene editor

## What was built

- `box.draw`, an immediate mode drawing module: `box_draw` draws a prefab or template instance for one frame, keyed so the instance keeps its identity between frames, on an overlay layer a dedicated view draws unlit over the scene when asked.
- `box.editor`, with the `Editor` singleton, the `editor [on|off]` command, the `EditorTree` and `EditorInspector` models the engine fills, the `editor.*` script functions and the gizmo math.
- `etc/editor/editor.flecs`, the overlay in flecs script: a toolbar, a tree of the entities with an icon per kind and a property grid with text inputs, checkboxes and dropdowns, all templates over the layout, text, shape and event components in the colors of the flecs playground.
- `etc/editor/gizmos.flecs`, the move, rotate and scale gizmos with single, double and triple axis handles and the control point handle of the spline editor.
- Selection through the picking events: `box.picking.Unhandled` receives every event nothing took, a strict view picks its own layers, a press a restarted task missed is kept for it, and one `TextInput` holds the focus at a time with `text.focus`.
- Instance handling: a child of a prefab or template instance selects its root, Open focuses the instance recursively by hiding the rest of the scene, and the transform of a template child is editable only when its statement holds literal values, in which case an edit is written back into the script.
- The spline editor: a handle per control point, a guide curve, and buttons to add and remove points.
- `Order`, a component of `box.ui` that places a child among the children of a layout, and module templates are no longer loaded incrementally.

## Verification

- `test/editor` (88 cases over Command, Select, Tree, Inspector, Source, Gizmo, Spline and Toolbar), `test/draw` (13 cases), and the new cases of `test/picking` and `test/ui`; every test project passes.
- Headless captures of the app driven over its HTTP terminal: `editor on`, a click on the ball, the Move, Rotate and Scale tools and a drag of the x arrow that moved the ball; see `gizmos/deliverable` and `editor.png`.
- The integration scenes: 106 visual and 17 command scenes pass; 17 visual and 5 command scenes fail on this branch and, run the same way, on the unmodified main tree (stale baselines and expected files after the recent renderer and Text changes), see `integration/`.

## Lessons

- A press and a hover in the same frame lose the press when the hover re-evaluates a template, because the re-evaluation restarts its async blocks; the picking update is an immediate system now and keeps a missed press for the restarted task.
- Script reactivity that adds or removes a component of the entity whose OnAdd or OnRemove it reacts to fails inside the table move; the gizmo templates keep every component in every branch and only change values.
- Interpolating an entity into a name gives its path, which turns into a hierarchy; rows are named by a numeric id.
- The command script runs all its lines in one frame, so a scene is driven over HTTP when steps need frames between them.
- See `docs/FLECS_NOTES.md` for the flecs issues met on the way.

## Not in this epic

- A dual axis rotation handle, which is not an established gizmo pattern; rotation has the three rings and the trackball.
- Editing vectors, arrays and pairs in the grid; they are shown as text.
- Undo, multi selection and snapping.
- Writing the edited script back to disk, which the checkpoint command already does.
