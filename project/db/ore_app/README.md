# Ore Else application and world

The apps/ore_else project on box: assets, engine setup, the terrain grid with deposits and props, and the player with mining and the camera.

## Details

The game lives in apps/ore_else with its own bake project, the SVG assets and the terrain generator
carried over, the engine scripts (surface, camera, sky, atmosphere, effects, quality tiers)
rewritten against box components, the map as a flat grid the game owns with deposits, boundary and
scenery, and the player walking, running, mining and demolishing with the camera following. It
builds native and wasm and runs headless for tests.

Verification: The app starts to its scene headless and in the browser, the visual scenes of the world and the
player are baselined, and the game module tests cover the grid, deposits and player rules.

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
