# Render thread stall at frame 270

Bench scenes with 50k template roots show one 4.8 ms frame in the render thread wait of BoxSurfaceAcquire around frame 270; find what the renderer does that frame and spread or remove it.

## Details

`picking_1m.flecs` and `first_person_1m.flecs` both wait 4.8 ms for the render thread once, at frame 270 with a 90 frame warmup, while every other frame is under a millisecond; `lod_city` with the same root count does not. Time the renderer's passes for that frame with `GpuTimings` and the CPU side with `SystemMonitor`, find the one shot work (a shadow cache rebuild, a Hi-Z readback, a buffer growth, a texture pager flush or a bake) and either spread it over frames under the job budget or move it to load time. Both scenes carry a spike factor of 12 to cover it; put them back to 8 when the frame is gone.

Verification: the two scenes pass at spike factor 8 through `python3 tools/integration.py bench`, recorded in the deliverable, every visual scene unchanged.

## References

- `project/db/picking/scale/deliverable/README.md`, `project/db/collision/controller_collision/deliverable/README.md`, `src/renderer/`
