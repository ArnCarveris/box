# Editor module and command

The box.editor module with its Editor singleton, the editor command, selection, the tree and inspector models and the script functions the editor UI calls.

## Details

The editor command sets Editor.enabled through box_editor_enable; the BoxEditorUpdate system returns at once while the editor is off, and turning it off closes the opened instances, gives the input maps back and clears the selection. The system refreshes EditorTree, the visible rows of the entity hierarchy with a kind per row, and EditorInspector, the property grid of the selection built from the reflection data of its components with a widget kind per member, and publishes them only when they changed.

box_editor_assign parses a value written as text through the meta cursor, so a text input, a checkbox or a dropdown of the overlay writes any member. The functions of the editor scope, editor.select, expand, open, close, tool, assign, grab, pull, drop, point, point_add and point_remove, are what the script calls from its async blocks.

Verified with the Command, Select, Tree and Inspector suites of test/editor.
