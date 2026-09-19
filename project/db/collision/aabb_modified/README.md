# Rendered bounds reach the collider

The renderer writes AABB without an OnSet event, so shapes that fall back to their bounds never get a Collider; mark the bounds modified and cover it with a test.

## Details

TODO
