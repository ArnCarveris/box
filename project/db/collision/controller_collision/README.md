# Controller collision

Controllers collide with scene geometry through the spatial index, verified on a million objects.

## Details

Collision sweeps a capsule against the spatial index with sliding resolution, a step up for small obstacles and a ground probe, so no system walks all objects and only the cells the capsule can reach this frame are queried. The same query path serves the camera controller's clearance and the first person controller. Collision can be disabled per controller for free flight.

Verification: unit tests cover wall sliding, step up, falling, standing on geometry and disabling collision. The brief's verification is a benchmark that navigates a scene with a million objects and must hold 60 FPS, kept as the deliverable report.
