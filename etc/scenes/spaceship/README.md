# Kestrel orbital service dock

A deep-range frigate secured in a two-sided orbital maintenance berth. The ship includes a furnished bridge, sensor mast, railgun, turrets, radiators, engine assemblies and ventral service bays. Four hydraulic clamps connect it to the dock; an aft service gantry joins the dock halves, with a control tower and cargo containers on the service platforms.

Run from the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/spaceship.flecs
```

The default camera shows the complete berth. Alternate cameras are `shipyard.aft_camera`, `shipyard.bridge_camera` and `shipyard.clamp_camera`:

```sh
bake3 run apps/boxrun --local-env -- --camera shipyard.aft_camera etc/scenes/spaceship.flecs
bake3 run apps/boxrun --local-env -- --headless --frames 48 --out kestrel.png etc/scenes/spaceship.flecs
bake3 run apps/boxrun --local-env -- --design etc/scenes/spaceship/inspect_ship.flecs
```

The `inspect_*.flecs` entry points isolate individual assemblies for design mode. Includes are relative to the scene files; textures resolve from `etc/scenes/spaceship/assets`. Keep the entry point and asset directory together when moving the scene.

Hull materials use an ORM texture with full occlusion visibility in red, the original roughness pattern in green and full metallic modulation in blue. The material supplies the base roughness and metallic factors. The main view uses GTAO, bloom, tonemapping and SMAA, with the supplied HDR starfield as its environment.
