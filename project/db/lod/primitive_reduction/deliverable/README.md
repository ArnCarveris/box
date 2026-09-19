# Primitive reduction

`box_lod_ladder` turns the authored segment count of a shape into its levels: level zero is the authored count, which
caps the ladder, and every level halves it down to the floor of its family, four segments or one subdivision of a
rounded box corner. `box_lod_chord_error` is the sagitta a level costs at the radius of the instance in world units,
which `box_lod_reduce` projects with the metric of the epic and turns into a level inside the band of
`box_lod_select`.

`src/geometry/mesh_lod.c` gives every sphere, hemisphere, cone, cylinder, torus, capsule, tube, frustum and rounded
box a ladder, and the head of the extract picks a level per frame from the bounds of the instance, the radius its
matrix scales the arc to and the views of the frame, taking the finest level any view asks for. A level is an ordinary
cache entry of the same key, so instances at one level share a mesh and a change is a handle swap. Reducing needs
`LodSettings` on the view or as a singleton and `enabled: false` turns it off, which leaves every earlier scene bit
identical; `lod_spheres.png` is the new one, rows of 32 segment spheres drawn at 32, 16, 8 and 4.

Verified: 1126 tests, 34 new in `lod.Reduce` and `geometry.Reduce`, lint 0, 75 visual scenes match. `select_10k_still`
picks levels for 10000 spheres in 179 us and reports 40.3M authored against 1.34M drawn triangles, `lod_field` draws
22000 instances at 1.47 ms a frame. Left out: a spline mesh is built per entity and not through the cache so its count
is a rebuild, an icosphere counts subdivisions, shadow views take the level of the main view, and the cache now
compares keys by value because padding could split a mesh.
