# Application skeleton

apps/ore_else on box with the engine, scene, config and quality scripts, a title screen and native and wasm builds.

## Details

Create apps/ore_else (project.json, main.c importing the box modules and the ore_else module, the
ore_else module registering the game components), port engine.flecs (Surface, Camera, sun, moon,
Stars, Atmosphere, the effect chain with GTAO, Bloom, AutoExposure and the tonemap), config.flecs,
quality.flecs as tiers switching effect and shadow settings, scene.flecs and the title screen that
waits for any key through an async on.pressed, and the --stresstest flag parsing (the base itself is
the release epic). Build native and wasm and run headless.

Verification: The app runs 60 headless frames on its scene in a test, the title scene is baselined, the build
reports are in the deliverable.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/main.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/game.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/engine.flecs
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/quality.flecs
- apps/box_app
