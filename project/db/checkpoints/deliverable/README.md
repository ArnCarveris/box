# Checkpoints: completion report

## What was built
- `ecs_script_reload(world, script, code)` in flecs (branch `template_values`, commit `37bcc0b28`): the new source is parsed before the old entities are cleared and handed to the existing update path, so a parse failure leaves the world untouched and only fills the script's error; `code == NULL` re-reads the script's file. `NOTES.md` under `script_reload` records the approach and its limits.
- `box.checkpoint`: `box_checkpoint_write` walks every managed script, records the current value of every component of the entities it owns through the flecs source preserving edit API and writes `<process folder>/checkpoints/<name>/<script>.flecs`, so comments, layout and untouched expressions survive byte for byte. The `Checkpoint` singleton redirects the folder and keeps `last` and `count`, `NoCheckpoint` keeps engine derived components out of the file, and entities no statement declares are reported, never dropped silently.
- `box_checkpoint_restore` parses every file of a checkpoint before the first reload, restores through `ecs_script_reload`, keeps the script that declares the running session, reports unowned entities and names the file that fails to evaluate. The `checkpoint [name]` and `restore [name]` commands expose both to terminals, HTTP and the inspector.
- The flecs edit API went linear (commit `736e76de5`): a line index built once per script and binary searched, a hashmap from entity and component to its edit, and using scopes resolved once per session. A 50k entity checkpoint went from 31.6 s to 27 ms, a flat 1.8 million entities a second.

## Verification
- 1474 unit tests across 18 projects, all passing; `test/checkpoint` is new with 40 tests over the `Folder`, `Report`, `Restore`, `Survive`, `Write` suites and `test/script` grew to 21 with its `Reload` suite; in flecs, `Reload` (12 cases) and `Edit.set_10k_statements` are new.
- Scenes in `integration/`: `checkpoint.log` checkpoints a scene, moves a crate, checkpoints again and adds an unowned entity; `checkpoint_restore.log` checkpoints, moves, deletes, adds an unowned entity and restores twice, with the world matching the checkpoint after each restore; every visual scene bit identical.
- Benchmarks in `bench/` against the picking epic: a checkpoint of 500 entities writes in 0.28 ms, 5000 in 2.6 ms and 50k in 27 ms; a restore of 500 entities takes 4.2 ms.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Parsing everything before deleting anything is the whole difference between a restore an agent can trust and one that empties the scene on a typo.
- Entity handles change across a reload; renderer slots, colliders, template roots and controllers all had to be proven to follow the recreated entities in a `Survive` suite.
- `$` singleton statements are not script owned, so a scene's settings survive a restore and are not written back.

## Not in this epic
- Handles are not preserved across a restore and a restore is atomic only up to the first file that fails to evaluate.
- Template body entities are counted, not written, because their statement is shared.
