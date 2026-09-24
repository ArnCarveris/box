# Transform gizmos

Move, rotate and scale gizmos with single, double and triple axis handles, drawn on top of the scene through box.draw and driven by script events.

## Details

etc/editor/gizmos.flecs defines MoveGizmo, RotateGizmo and ScaleGizmo as templates of Handle instances, each with a GizmoHandle that names its mode and axes: arrows, plane squares and a center cube for moving and scaling, rings and a trackball for rotating. A handle awaits on.press and on.drag and calls editor.grab and editor.pull, so the interaction is script and the projection of the cursor ray onto an axis, a plane or a rotation angle is native.

The gizmo of the tool is drawn every frame at the selection, sized by its distance to the camera, in the overlay layer so it is seen through geometry, and the release of the button drops the grab and writes a template child back to its script.

Verified with the Gizmo suite of test/editor and the captures in the deliverable.
