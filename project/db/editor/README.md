# Scene editor

An in game editor, switched on with the `editor` command, that selects scene objects, shows the entity tree and a property grid, and moves, rotates and scales the selection with gizmos drawn over the scene.

## Details

The editor is the box.editor module with an Editor singleton and two models the engine fills, EditorTree and EditorInspector. The overlay is entirely flecs script, etc/editor/editor.flecs, built from the layout, text, shape and event components of box.ui in the colors of the flecs playground; its widgets call the editor.* functions from their async blocks. The gizmos are templates in etc/editor/gizmos.flecs drawn every frame through the immediate mode box.draw module, on an overlay layer a dedicated view renders unlit over the scene.

Selection goes through the picking events of flecs script: a click nothing took reaches the Unhandled sink and the overlay selects what it hit, resolving the children of a prefab or template instance to their root. Open focuses an instance by hiding the rest of the scene, recursively, and the source of a template child is scanned so only literal transforms are editable; an edit is written back into the script through the flecs edit API. A selected Spline gets a handle per control point, a guide curve and buttons to add and remove points.

Verified with the test/editor project, the test/draw project, the new cases of test/picking and test/ui, and headless captures of the app driven over its HTTP terminal.
