# Responsiveness

A job system, non fragmenting hierarchies, incremental template loading near the camera first and background rasterization, so big workloads never spike a frame.

## Details

The rule the brief sets is that a large scene may run slowly but must never stall: no frame spikes from loading, baking, rasterizing or generating. This epic builds the shared machinery for that, a prioritised job system with main thread completion and a per frame budget, incremental template instantiation ordered by distance to the camera while non template content still loads immediately, and SVG rasterization moved onto jobs. It also adds support for the non fragmenting Parent relationship next to ChildOf, which the brief requires and which keeps table counts sane in the million object scenes the other epics verify against.

Verification is unit tests for the job queue, the instantiation budget and ordering, and the hierarchy semantics, plus a spike harness that reports the worst frame alongside the mean and fails when a frame exceeds a multiple of it. The harness is reused by the LOD, lighting and particle epics. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: JobSettings and the job status components, the incremental loading settings, and Parent hierarchy support in transform, bounds and extraction.

## References

- design/DESIGN.md, section Responsiveness and Content (Parent and ChildOf hierarchies)
- flecs commit 355b18260b43b1f2bf672fccb9d3e0726f3682aa for incremental instantiation
