# Ore Else release

The stress test mode, quality tiers, checkpoints, a scripted playthrough, the performance pass and the web publish.

## Details

The stress test flag builds the late game base and advances the clock; quality tiers switch shadow
size, occlusion, bloom and resolution scale; the box checkpoint commands save and restore a game; a
command based playthrough covers crash landing to launch; bench scenes for the stress test at 30 and
45 minutes run spike free; the wasm image is published the way the README describes.

Verification: Every bench scene spike free at the standard budget, the playthrough log as a deliverable, the wasm
image in the deliverable and the README of the game rewritten for box.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md
