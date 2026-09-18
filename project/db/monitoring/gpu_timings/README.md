# GPU timings

GPU timestamp queries per render pass, returned to the world as a component.

## Details

The renderer writes timestamp queries around each pass when Surface.gpu_timings is set, resolves them a frame or two later and returns the results with the packet, where a GpuTimings component exposes them per pass name. Pass names come from the views and the effect graph, so a scene can see what each effect costs. Missing timestamp support degrades to no data instead of an error.

Verification: unit tests cover the plumbing with a stub renderer, the per pass naming and the delayed resolve. An integration run captures timings for a scene with several effects and keeps the JSON as a deliverable.
