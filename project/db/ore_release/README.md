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

## Porting rule

Ore Else was built on an older, inferior engine, and this port is the showcase for how box does it
better. Do not carry the old engine's structure, workarounds or C where box has a declarative way:
prefer script templates, components and the command interface over C systems, keep gameplay logic
in C only where it is genuinely simulation (grids, economy, AI), and where the old game worked around
a missing feature, file the feature as a box task instead of porting the workaround. Every screen,
effect and building should read as an example of the box way.
