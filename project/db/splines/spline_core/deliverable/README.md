# Spline component

`box.spline` is a new module. `Spline` holds a reflected vector of control
points, a type (catmull rom, linear or bezier), a tension and a loop flag.
Setting it rebuilds a cached arc length table (16 samples per segment) and
writes `SplineInfo` with the length and the segment count. The module evaluates
position, tangent and normal by parameter and by distance; the normal is the up
of the curve, perpendicular to the tangent in the plane it spans with world up.

Scripts sample a curve with `spline.position`, `spline.tangent`,
`spline.normal`, `spline.point_at`, `spline.tangent_at`, `spline.normal_at` and
`spline.length` under `using box`. Other modules use the same sampling through
`src/spline/spline_internal.h`.

Verified with 37 unit tests in `test/spline` (Eval, ArcLength, Cache,
Degenerate, Script): analytic values per curve type, arc length parametrisation,
closed loops, single segment and degenerate inputs, cache invalidation and
removal, and a script that authors a curve and calls the functions. Six
benchmarks in `bench/spline` measure sampling throughput and table rebuilds.
`tools/lint.py` reports 0 problems.

Left out: the visual scene, which lands with `splines/spline_paths` because it
shows objects following the curve; the README and architecture notes, which are
outside the files of this task.
