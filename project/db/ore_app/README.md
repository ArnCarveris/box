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
