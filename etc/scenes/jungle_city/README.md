# Canopy City

A procedural jungle utopia on a finite earthen diorama: 90 buildings mixing terraced residential towers, cylindrical garden towers, stepped residences, slender solar-canopy spires and staggered pod residences, timber facade fins and soffits, planted balconies, hanging vines, rooftop gardens and solar panels, connected elevated walks, a central water garden and a ribbed botanical conservatory. Layered trees and understory ferns weave through the districts.

The primitive survey in [INVENTORY.md](INVENTORY.md) was written before the scene design. All assets are native Flecs geometry in separate files; no external textures or models are required.

## Run

From the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/jungle_city.flecs
```

Hold the right mouse button and use WASD/QE to explore. The initial perspective camera frames the diorama from above.

## Configure

Set properties on the `assets.JungleCity` instance in `../jungle_city.flecs`, following the `assets.Town` pattern:

```flecs
assets.JungleCity city(rows: 9, columns: 10, seed: 17) { camera {} }
```

The reusable template is in [city.flecs](city.flecs). Include `jungle_city/city` to use it in another scene. Each instance has independent dimensions, seed and vegetation settings; there are no global configuration constants. The plinth, water, forest perimeter, bridge network, camera distance and far plane follow the dimensions. The camera child is declared in the instance body so the view can reference it before template expansion. The ambient sky, light bake and view remain in the entry scene. Streetlamps and signs belong to each city instance.

For a wider city:

```flecs
assets.JungleCity city(rows: 4, columns: 8, spacing: 18, seed: 42,
  base_floors: 2, floor_variation: 2, center_floors: 8, trees_per_block: 5) { camera {} }
```

| Setting | Default | Supported values / meaning |
| --- | --- | --- |
| columns | 10 | Integer at least 2; odd counts reserve the middle column for the canal |
| rows | 9 | Integer at least 2 |
| spacing | 17 | At least 15 world units between districts |
| seed | 17 | Nonnegative integer; deterministic lot, architecture and vegetation random streams |
| base_floors | 2 | At least 2 |
| floor_variation | 2 | At least 1; random additional floors from zero up to this value minus one |
| center_floors | 8 | Additional floors at the center, tapering smoothly to zero at the perimeter; keep base + variation + center at most 13 |
| trees_per_block | 7 | Nonnegative integer; reduce for more open districts or faster loading |

Default footprint: 202 by 173 world units, approximately three times the original 122 × 95 area, with exactly three times as many buildings (90 versus 30). Width is columns × spacing + 32; depth is rows × spacing + 20. Even layouts have columns × rows towers; odd layouts have (columns − 1) × rows towers. For example, 8 columns, 4 rows and spacing 18 produces a 176 × 92 diorama with 32 towers. Larger counts increase geometry and startup cost; no runtime LOD is implemented.

## Asset files

`materials.flecs` contains shared material prefabs; `leaf.flecs` defines a folded leaf mesh shared by trees. `tree`, `branch`, `fern`, `vine`, `tower`, `rotunda`, `cascade`, `spire`, `pods`, `roof`, `streetlight`, `sign`, `bridge`, `conservatory` and `terrain` each define one reusable asset template. `districts.flecs` arranges those assets. The city template loads dependencies in order and provides a camera; the entry scene defines lighting and effects. Asset templates expect those shared dependencies to be loaded first.

Opaque teal glazing keeps the dense city in the opaque render pass. A subdued ambient sky and emissive surfaces feed a whole-city indirect light bake with one bounce, six-unit probe spacing and 32-pixel captures. The interactive bake uses an eight-millisecond frame budget and caches completed results. GTAO, subtle height fog, restrained bloom, tonemapping and SMAA finish the image. The first bake takes longer than a cached load; headless rendering completes the bake before producing the image.

## Building variation

Each lot seeds Box's `math.Rng` using the city seed and both coordinates. Separate draws select five building families, floor count, width, depth, five wall colors, three timber colors, facade spacing and six roof treatments. Weighted family arrays selected with a Flecs `match` expression favor towers and spires in the center and terraced or pod residences outside. A smooth product of the normalized X/Z falloffs adds up to eight floors near the center, while the perimeter stays at two or three floors. Vegetation has its own stream, so changing tree density does not reshuffle buildings. Lot random streams stay fixed at existing grid coordinates when resizing; the height gradient and family weights adapt to the new center. Window illumination uses an independent seeded stream in each building. Selection no longer uses a coordinate modulo pattern.

Terrace towers have balcony bands, rotundas have circular floor plates, cascade residences have receding terraces, spires have tapered cores and widely spaced decks, and pod residences have staggered rounded volumes. Roofs mix gardens with solar panels, pergolas, ribbed greenhouses, raised solar arrays, reflecting pools and shaded rooftop pavilions. All families connect to the pedestrian network at a common height.

## Vegetation and city lighting

Trees use tapered trunks, buttress roots, visible branches, twigs and individual folded leaf meshes. Randomized broadleaf crowns and arching palm fronds replace solid foliage spheres. Tree height, species, branch direction and leaf dimensions vary independently.

Warm emissive window panels appear in a seeded subset of rooms, with solar-topped emissive streetlamps, softly illuminated destination signs and subdued jade bridge edges. Rooftop pools and pavilions add occasional warm accents. These surfaces illuminate their surroundings through the light bake; there are no point lights or directional lights in either entry scene. Ambient sky illumination keeps unlit foliage readable.

To inspect the city lighting at dusk, run the alternate entry scene (it includes the same city and overrides only presentation settings):

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/jungle_city_dusk.flecs
```

## Validation

Build with `bake3 --local-env` and run the core checks with `bake3 run test/cpu/core --local-env -- -j 8`. Preview generation:

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/jungle_city.flecs --headless --frames 4 --port 0 --out /tmp/jungle-city.png
```
