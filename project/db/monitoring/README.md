# Monitoring

Built in CPU, ECS system, GPU and flecs statistics monitoring, exposed as components rather than as new endpoints.

## Details

Monitoring is the brief's requirement that the framework measures itself: process and machine CPU usage, per system and per phase ECS timings, GPU timestamps per render pass and the flecs statistics addon. All of it is exposed as components, so the existing REST API, the terminal and the status page read it without new endpoints, and each source is enabled by a component so it costs nothing when off. The CPU numbers are shared with the CPU budget that headless instances already honour.

Verification is unit tests per source, integration tests that enable a monitor and read its component from a terminal session, and benchmarks for the per frame cost of each source when it is on. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: CpuMonitor, SystemMonitor, GpuTimings and EcsStats.

## References

- design/DESIGN.md, sections Agent based development (monitoring) and Command interface
