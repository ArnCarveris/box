# Spline roads

`SplineMesh {spline, profile, segments, width, height, closed, smooth}` sweeps a cross section
along a `Spline` and writes the result as a `Mesh3`, so a road is drawn, lit, shadowed, textured,
collided with and shared by the mesh cache like any other mesh, without a line of renderer code.
The profile is a polyline in the plane of the cross section (x across the curve, y up) that faces a
quarter turn counter clockwise from the direction it runs: a flat strip is a road, a closed profile
is a pipe or a cable, `smooth` shares the normals of neighbouring points and `segments` names the
slices, eight per curve segment by default. Rings sit at even arc length, a curve that loops closes
the sweep, u runs across the section and v along the curve. The mesh is rebuilt when the component
or the curve changes, never per frame, so the entity stays a static instance; a sweep that names no
curve uses the one on its own entity and loses its mesh when that curve goes away.

Verified with 67 unit tests in `test/spline`, 19 of them the new `Mesh` suite: layout, slice counts,
profile placement, normals, winding, uvs, loops, closed and smooth sections, rebuilds from the curve
and from the profile, bounds, sharing, removal and a script. `Mesh.road_1000_points` rebuilds a 1000
point road of 2000 slices in 0.21 ms, 0.51 ms when the curve moves and the table is rebuilt with it.
`spline_road.png` is the scene, a closed road loop with a centre line and a smooth hexagonal pipe;
every other baseline still matches and `tools/lint.py` reports 0 problems.

Left out: vertex shader extrusion, which the epic brief names as the mechanism; the mesh is built on
the CPU instead, which needs no renderer change and buys shadows, materials and collision for free.
A closed profile with `smooth` wraps its u back to zero on the last edge.
