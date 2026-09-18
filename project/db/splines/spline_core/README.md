# Spline component

A reusable spline entity with control points, evaluation by parameter and by arc length, and a cached length table.

## Details

Spline holds control points and a type, catmull rom, bezier or linear, plus tension and a loop flag; the module evaluates position, tangent and normal by parameter and by arc length with a cached length table that is rebuilt when the points change. Splines are entities so a road, an animation and a particle emitter can share one curve. Sampling is exposed through components rather than public native functions so scripts can use it.

Verification: unit tests compare evaluation against analytic values, cover arc length parametrisation, closed loops, single segment and degenerate inputs and cache invalidation. A visual scene draws a spline with Line segments, and a benchmark measures sampling throughput.
