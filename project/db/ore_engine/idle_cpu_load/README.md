# box_app burns 700% CPU on the default scene

A windowed box_app on the default scene sits at 700% CPU while nothing moves; an idle frame must cost one core at most and the workers must sleep.

## Details

The job workers in `src/job/pool.c` block on a condition when the queue is empty, so seven
cores busy means work is being queued every frame, or several threads are spinning
elsewhere. Candidates: a job that re-submits itself or whose result is discarded and
regenerated every frame (light baking, the IBL filter, LOD builds, mesh generation, texture
uploads, SVG rasterization, script evaluation of an incremental template), flecs worker
threads if the pipeline is multithreaded, the renderer thread presenting without vsync
(`WGPUPresentMode_Immediate` when `Surface.vsync` is off) and rendering as fast as it can, the
command server polling, or the CPU budget pacing that only engages when `--cpu-budget` is
under one. Measure first: run the default scene headless with `--timeout 10` and the system
monitor and job stats commands (`project/db/monitoring`), or sample the process with
`sample <pid> 5` on macOS, and attribute the threads before touching anything. Fix the actual
source: a job that must not repeat, a present mode that defaults to vsync in a window, or a
thread that should wait. Do not paper over it with a sleep.

Verification: a `test/job` or `test/monitor` case that an idle default scene queues no jobs
after the first frames settle, the `bench/job` or the affected module's bench attached, and a
headless run of the default scene with `--timeout 10` whose reported CPU time stays under the
wall time times one core (state both numbers in the deliverable, with the thread attribution
from the sample before and after).

## References

- `src/job/pool.c`, `src/core/run.c`, `src/renderer/device.c`, `src/surface/surface.c`,
  `project/db/responsiveness/deliverable/README.md`, `project/db/monitoring/deliverable/README.md`
