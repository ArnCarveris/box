# Editor UI script

The editor overlay in flecs script: toolbar, tree view with kind icons and a property grid of text inputs, dropdowns and checkboxes.

## Details

etc/editor/editor.flecs loads at import into box.editor.ui and brings the overlay into being while Editor.enabled is set: a toolbar with the tool buttons and the Open, Close and spline buttons, a tree panel whose rows carry an icon per kind in the colors of the flecs playground, and an inspector with a header row per component and a row per member. Every widget is a template over the layout, text, shape and event components that calls the editor.* functions from its async blocks, and the panels consume the events on them so they do not reach the scene.

The rows of the tree keep the order of the model through the new Order component of box.ui, which a row or a column sorts its children by, and a text input of the grid carries an EditorBinding the engine applies when the line is submitted.

Verified with the Toolbar suite and the widget cases of the Tree and Inspector suites of test/editor, and the capture in the deliverable.
