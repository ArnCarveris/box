# Monitoring and checkpoints

Built in CPU, system, GPU and ECS monitoring, and script based checkpoints that an agent can create and restore.

## Details

Monitoring is the brief's requirement that the framework measures itself: process and machine CPU usage, per system and per phase ECS timings, GPU timestamps per render pass and the flecs statistics addon. All of it is exposed as components, so the existing REST API, the terminal and the status page read it without new endpoints, and each source is enabled by a component so it costs nothing when off.

Checkpoints give agents a way back: a checkpoint iterates the loaded scripts, updates them with the current state of the entities they own and writes them into a per instance folder that is removed when the instance stops, and restoring reloads those files. Restoring needs an ecs_script_reload entry point that does not exist yet and is implemented in the flecs repository with its own tests and commit. Checkpoints may assume static scenes and entities that no script defines are reported as not checkpointable. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: CpuMonitor, SystemMonitor, GpuTimings and EcsStats, plus Checkpoint and the checkpoint and restore command functions.

## References

- design/DESIGN.md, sections Agent based development (monitoring, checkpoints) and Command interface
- flecs script addon, which gains ecs_script_reload in this epic
