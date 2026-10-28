# Kestrel orbital service dock

A deep-range frigate undergoing an overhaul in an orbital maintenance berth. The overhead service gantry carries a suspended cargo container above the port engine, with a hoist trolley, winch drums, a cab, access platforms, a caged ladder, and a utility bridge. Four hydraulic clamps secure the ship.

The eight dock bays alternate between cryogenic supply, electrical service, cargo handling, and component repair. Numbered deck markings identify the work areas. Tanks, transformers, cable reels, stacked freight, a tow tractor, spare engine hardware, and suited crew establish their function and scale. The outer platforms carry habitation, tank farms, and cooling equipment; an aft catwalk joins the dock halves below the control tower.

The ship uses large fitted armor courses with contrasting ceramic, metal, and replacement panels. An open port maintenance bay, starboard avionics housing, dorsal heat exchangers, launch-cell covers, access hatches, and coolant manifolds break up the hull. The bridge, sensors, railgun, turrets, radiators, engines, and ventral service bays remain individually inspectable.

Run from the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/spaceship.flecs
```

The default camera shows the complete berth. Alternate cameras are `shipyard.aft_camera`, `shipyard.bridge_camera`, `shipyard.clamp_camera`, `shipyard.gantry_camera`, and `shipyard.hull_camera`:

```sh
bake3 run apps/boxrun --local-env -- --camera shipyard.gantry_camera etc/scenes/spaceship.flecs
bake3 run apps/boxrun --local-env -- --camera shipyard.hull_camera etc/scenes/spaceship.flecs
bake3 run apps/boxrun --local-env -- --headless --frames 48 --out kestrel.png etc/scenes/spaceship.flecs
bake3 run apps/boxrun --local-env -- --design etc/scenes/spaceship/inspect_ship.flecs
bake3 run apps/boxrun --local-env -- --design etc/scenes/spaceship/inspect_gantry.flecs
```

The `inspect_*.flecs` entry points isolate assemblies for design mode. `inspect_dock.flecs` includes the gantry and service structures. Includes are relative to the scene files; textures resolve from `etc/scenes/spaceship/assets`. Keep the entry point and asset directory together when moving the scene.

Hull materials use muted base colors and an ORM texture for roughness variation, without the repeating albedo and normal panel pattern. Armor seams, hatches, and machinery are geometry. The main view uses GTAO, bloom, tonemapping, and SMAA, with the supplied HDR starfield as its environment.

Shapes positioned at the origin of a parent assembly explicitly declare `Position: {}` so they inherit its transform. Repeated named details use an anonymous scope inside each loop to create separate entities.
