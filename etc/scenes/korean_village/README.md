# Korean village

A courtyard diorama with a dancheong hall and entrance gate, four timber-and-plaster
homes, tiled enclosure walls, five sculpted pines, a bamboo grove, a stone pagoda, a
roofed well, stone and silk lanterns, a bench, an onggi storage terrace, firewood,
drying persimmons, and a stream with a slab bridge guarded by two jangseung. The hall
and gate carry the strongest painted colors; the houses use quieter timber, paper,
and plaster materials.

Run from the repository root:

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/korean_village.flecs
```

Inspect the hall's painted brackets, floral panels, lattice doors, and roof tiles,
or walk the courtyard at eye level:

```sh
bake3 run apps/boxrun --local-env -- --camera korean_village.detail_camera etc/scenes/korean_village.flecs
bake3 run apps/boxrun --local-env -- --camera korean_village.courtyard_camera etc/scenes/korean_village.flecs
```

The options panel shared with `town.flecs` and `wild_wild_west.flecs` (`../cfg/hud.flecs`)
drives the time of day, day of year, latitude, north, fog density, shadows, baked lighting
and GTAO. The scene opens on a September afternoon at 36° latitude; move the time of day
past sunset to see the courtyard lit by its lanterns and lit paper doors.

## Engine survey and design

The survey covered `include/box/geometry.h`, `include/box/material.h`, the geometry
integration scene, and the Japanese garden, Santorini, and Wild Wild West scenes.

| Available feature | Use in this scene |
| --- | --- |
| Boxes, rounded boxes, cylinders, spheres, frustums, tubes, tori, icospheres | Joinery, masonry, lattice, tile caps, lanterns, foliage, and well |
| Mesh3, Path2D/3D, sweeps, lofts, profile extrusions, lathes, partial shells | A shared Mesh3 roof skin and a Bezier-profile lathed jar body |
| PBR roughness/metalness, clear coat, emission, transmission, textures, surface materials | Matte painted wood and plaster, rough tiles, coated brown pottery, emissive lantern interiors, reflective well water |
| Atmosphere/time of day, height fog, baked indirect light, reflection baking, shadows, GTAO, bloom, auto exposure, tonemapping, SMAA | Afternoon illumination, light haze, soft indirect fill, contact shadows, pottery and stream highlights, restrained lantern glow, exposure from day to night, and antialiasing |
| Point lights | Stone and silk lanterns, which take over from the sun after dusk |
| Widgets | The options panel in `../cfg/hud.flecs` |

The existing detailed scenes establish the fidelity target: individually modeled
roof ribs and caps, door lattices and hardware, structural brackets, paving,
masonry joints, vessel rims, and layered tree crowns. No external image assets
or engine changes are required.

## Assets

Each reusable asset lives in its own `.flecs` file and includes its dependencies.
The main scene contains placement, cameras, lighting, and effects.

- `hall`, `house`, `gate`, `wall`: architecture.
- `roof`, `roof_surface`: reusable curved hip roof, tile ribs, stamped eave caps,
  rafters, corner ridges, and ridge terminals. `ornate` adds ridge figures and wind
  bells; `rafter` and `rafter_end` paint the rafters.
- `bracket`, `lotus`, `door`, `beam`, `signboard`: architectural details and connecting
  geometry.
- `onggi`, `lantern`, `silk_lantern`, `well`, `bench`, `pagoda`, `jangseung`, `firewood`,
  `persimmons`: courtyard props.
- `pine`, `bamboo`, `shrub`: vegetation and mossy rocks.
- `landscape`: raised diorama base, paths, ground cover, stones, grass, stream, and
  bridge.
- `materials`: shared material prefabs.

The roof skin is a shared mesh sampled over four curved patches. Its nominal
footprint is 8.4 by 5.8, with a 5.3-wide ridge and a 1.65 rise; `roof.flecs`
uses the same profile for its ribs. Resize roof instances with `Scale`. Wall
length, tile and stone counts, jar size, pine branch orientation, bamboo height,
and whether a house dries persimmons are template properties.
