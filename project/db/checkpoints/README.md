# Checkpoints

Script based checkpoints that an agent can write and restore, built on a new flecs script reload entry point.

## Details

Checkpoints give agents a way back: a checkpoint iterates the loaded scripts, updates them with the current state of the entities they own and writes them into a per instance folder that is removed when the instance stops, and restoring reloads those files. Restoring needs an ecs_script_reload entry point that does not exist yet and is implemented in the flecs repository with its own tests and commit. Checkpoints may assume static scenes, and entities that no script defines are reported as not checkpointable.

Verification is unit tests for the write and the restore path, integration tests that checkpoint an instance, change the scene and restore it from a terminal session, and a benchmark for the write and restore time of a large scene. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Checkpoint, plus the checkpoint and restore command functions.

## References

- design/DESIGN.md, sections Agent based development (checkpoints) and Command interface
- flecs script addon, which gains ecs_script_reload in this epic
