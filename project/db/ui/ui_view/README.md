# UI view and trees

UI trees rendered through a view with an orthographic camera, children always drawn over their parents.

## Details

An entity with a Ui component roots a UI tree; everything below it is rendered by a UI view with an orthographic camera in logical pixels, composited over the 3D views. Depth inside a tree comes from the traversal order rather than from positions, so a child is always over its parent without z fighting or authored offsets. UI reuses Position, Rotation, Scale and the 2D shapes, so nothing in the geometry path is UI specific, and the view follows the surface size and resolution scale.

Verification: unit tests cover tree detection, draw order for nested and reordered children, the orthographic projection, resizing and several trees in one scene. A visual scene renders nested panels with overlapping children and is captured at two surface sizes.
