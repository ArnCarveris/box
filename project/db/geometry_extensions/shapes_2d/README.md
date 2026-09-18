# 2D shapes

Circle, Border with per corner radii, Line and Image showing a region of a texture.

## Details

Circle is a filled disc whose segment count follows its projected size, Border adds rounded corners and per edge widths and its own color to a Rect, Line is a thick segment in local coordinates and Image draws a texture region with a fit mode and a rotation. Border and rounded corners are evaluated analytically in the fragment shader so changing a radius does not rebuild a mesh. All four carry UVs and work with the material system like any other shape, and they are what the UI epic builds on.

Verification: unit tests cover the components, mesh selection, the analytic coverage function at known points and the Image UV region and fit modes. A visual scene renders a sheet with every 2D shape, a range of border radii and an image region, kept as a deliverable.

Introduces Circle, Border, Line and Image from the geometry datamodel.
