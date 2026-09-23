# Spline editor

Control point handles and a guide curve for a selected Spline, with adding, removing and dragging points.

## Details

A selected entity with a Spline gets a PointHandle drawn on every control point, keyed by the index, and a guide entity with a SplineMesh over the curve in the overlay layer. Pressing a handle selects its point and dragging it moves the point in the plane that faces the camera, mapped back into the space of the spline entity; the toolbar adds a point after the selected one, or extends the end, and removes the selected one.

Verified with the Spline suite of test/editor.
