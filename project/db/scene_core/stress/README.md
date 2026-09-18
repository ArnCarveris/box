# Stress tests and demo

100k static plus 10k dynamic entity scenes at 60 FPS, a primitives demo scene and the wasm build.

## Details

`apps/box_bench` loads a scene headless, moves or recolors between 1 and 1000 random static entities every frame and reports mean, p50, p99 and worst frame times of `ecs_progress`. `test/integration/bench/static_100k.flecs` spawns 100k static boxes and 10k falling, spinning spheres with `for` loops; `material_100k.flecs` gives the static boxes 1000 material variants and adds 10k `DynamicMaterial` boxes.

Release results on this machine at 1280x720 headless: static scene mean 1.25 ms, p99 2.6 ms; material scene mean 1.23 ms, p99 2.6 ms, both far above 60 FPS. Loading 110k entities from script takes 1.4 s. `static_100k.png` is a render of the static scene.
