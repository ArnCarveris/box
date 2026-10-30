# Dry Creek, 1876

A frontier rail stop on a sandstone display base. The Golden Spur saloon, general store,
and sheriff's office face a rutted main street. A covered freight wagon waits beside the
boardwalk; a windmill and timber water tower serve the horse corral. A handcar, telegraph
line, stacked supplies, and townspeople complete the railway frontage.

From the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/wild_wild_west.flecs
```

The default camera frames the entire diorama. Two additional cameras inspect the street
and the corral:

```sh
bake3 run apps/boxrun --local-env -- --camera wild_wild_west.street_camera etc/scenes/wild_wild_west.flecs
bake3 run apps/boxrun --local-env -- --camera wild_wild_west.corral_camera etc/scenes/wild_wild_west.flecs
bake3 run apps/boxrun --local-env -- --headless --frames 48 --out dry_creek.png etc/scenes/wild_wild_west.flecs
```

## Engine survey and design

The design follows the modeled detail and miniature presentation of `cheese_market.flecs`,
`santorini.flecs`, and the modular assets in `spaceship/`. The engine headers and visual
fixtures were surveyed before choosing the assemblies and rendering setup.

| Engine feature | Use in this scene |
| --- | --- |
| Box, rounded box, cylinder, capsule, sphere, cone, triangular prism, star | Buildings, trim, porch hardware, people, horses, cactus, sheriff's badge |
| Tube with partial arc, torus, frustum | Open wagon canopy and ribs, wheel tires, barrel hoops, lanterns |
| Path2D and Lathe | Curved barrel bodies |
| BeveledPrism, IcoSphere | Layered sandstone, windmill blades, scattered stones |
| PBR metallic and roughness, shared material prefabs | Weathered timber, painted siding, iron, steel, brass, canvas, sand |
| ClearCoat, baked reflections | Window panes and trough water |
| World-space Text with a custom Font | Shop signs, town marker, wanted poster in Rye |
| Atmosphere, TimeOfDay, HeightFog | Dusty desert sky, a July afternoon sun at 34° latitude, haze |
| Directional, point and emissive lighting | Afternoon sun, lanterns |
| LightBake, ReflectionBake, GTAO, bloom, auto exposure, tonemapping, SMAA | Indirect light, contact shadows, restrained lantern glow, smooth edges |
| Widgets | The options panel shared with `town.flecs` in `../cfg/hud.flecs` |

The survey also covered Mesh3, profile extrusion, sweep, loft, ellipsoid/torus shells,
texture maps, and transmission. The selected shapes keep the assets
self-contained and readable at the default camera distance. All details are geometry or
engine text. The only external asset is the Rye font in `fonts/`, under the SIL Open Font
License; no texture or mesh downloads are required.

## Assets

Each asset is a reusable `@tree Parent` template in its own `.flecs` file. Includes resolve
relative to that file, and declare their own module imports. Keep the entry point next to
this directory when moving the scene.

- Buildings: `saloon`, `general_store`, `sheriff`; shared `storefront`, `window`, `sign`, `lantern`.
- Railway and utilities: `railway`, `handcar`, `telegraph`, `water_tower`, `windmill`.
- Street and corral: `wagon`, `wheel`, `horse`, `cowboy`, `corral`, `fence`, `water_trough`, `hitching_rail`.
- Supplies and scenery: `barrel`, `crate`, `freight`, `town_marker`, `landscape`, `mesa`, `cactus`, `scrub`, `rut`.
- Shared construction: `materials`, `beam`.

The `inspect_*.flecs` entry points isolate the saloon, wagon, and windmill for design mode:

```sh
bake3 run apps/boxrun --local-env -- --design etc/scenes/wild_wild_west/inspect_wagon.flecs
bake3 run apps/boxrun --local-env -- --design etc/scenes/wild_wild_west/inspect_saloon.flecs
bake3 run apps/boxrun --local-env -- --design etc/scenes/wild_wild_west/inspect_windmill.flecs
```
