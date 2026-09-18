# Checkpoint creation

A checkpoint command that writes the current state of every loaded script into a per instance folder.

## Details

A checkpoint iterates the loaded scripts, updates each with the current values of the entities it owns and writes the result into a folder under the process folder, returning the path; the folder is removed when the instance stops. Entities that no script defines cannot be checkpointed and are reported in the command output rather than silently dropped. Checkpoints assume static scenes, as the brief allows.

Verification: unit tests cover writing, the per instance folder, cleanup on exit, the report of unowned entities and repeated checkpoints. A command based integration test creates a checkpoint, changes the scene and keeps the log and the checkpoint folder as deliverables.
