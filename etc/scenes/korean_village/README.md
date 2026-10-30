# Korean village

A courtyard diorama with a dancheong hall and entrance gate, four timber-and-plaster
homes, tiled enclosure walls, five sculpted pines, a well, stone lanterns, a bench,
and an onggi storage terrace. The hall and gate carry the strongest painted colors;
the houses use quieter timber, paper, and plaster materials.

Run from the repository root:

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/korean_village.flecs
```

Inspect the hall's painted brackets, floral panels, lattice doors, and roof tiles:

```sh
bake3 run apps/boxrun --local-env -- --camera korean_village.detail_camera etc/scenes/korean_village.flecs
```

## Engine survey and design

The survey covered `include/box/geometry.h`, `include/box/material.h`, the geometry
integration scene, and the Japanese garden, Santorini, and Wild Wild West scenes.

| Available feature | Use in this scene |
| --- | --- |
| Boxes, rounded boxes, cylinders, spheres, frustums, tubes, tori, icospheres | Joinery, masonry, lattice, tile caps, lanterns, foliage, and well |
| Mesh3, Path2D/3D, sweeps, lofts, profile extrusions, lathes, partial shells | A shared Mesh3 roof skin and a Bezier-profile lathed jar body |
| PBR roughness/metalness, clear coat, emission, transmission, textures, surface materials | Matte painted wood and plaster, rough tiles, coated brown pottery, emissive lantern interiors, reflective well water |
| Atmosphere/time of day, baked indirect light, reflection baking, shadows, GTAO, bloom, tonemapping, SMAA | Afternoon illumination, soft indirect fill, contact shadows, pottery highlights, restrained lantern glow, and antialiasing |

The existing detailed scenes establish the fidelity target: individually modeled
roof ribs and caps, door lattices and hardware, structural brackets, paving,
masonry joints, vessel rims, and layered tree crowns. No external image assets
or engine changes are required.

## Assets

Each reusable asset lives in its own `.flecs` file and includes its dependencies.
The main scene contains placement, cameras, lighting, and effects.

- `hall`, `house`, `gate`, `wall`: architecture.
- `roof`, `roof_surface`: reusable curved hip roof, tile ribs, stamped eave caps,
  corner ridges, and ridge terminals.
- `bracket`, `lotus`, `door`, `beam`: architectural details and connecting geometry.
- `onggi`, `lantern`, `well`, `bench`, `pine`: courtyard props and vegetation.
- `landscape`: raised diorama base, paths, ground cover, stones, and grass.
- `materials`: shared material prefabs.

The roof skin is a shared mesh sampled over four curved patches. Its nominal
footprint is 8.4 by 5.8, with a 5.3-wide ridge and a 1.65 rise; `roof.flecs`
uses the same profile for its ribs. Resize roof instances with `Scale`. Wall
length, jar size, and pine branch orientation are template properties.
