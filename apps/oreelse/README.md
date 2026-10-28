# Ore Else

You crash-landed on a hostile planet. Mine ore, craft a supply chain, defend the base against
critter waves, and build rockets to ship Luminite into orbit. Install a life support unit on a
rocket and you leave with it (win). You lose when the swarm kills you. The waves grow forever.

The game is played top down on a 48x48 tile grid, with a camera that follows the astronaut. It
is the port of flecs-engine's ore_else to box; the original's sources are in
`reference/ore_else` and `reference/PORTING.md` maps the features it uses to box.

## Build, run and test

From the repository root:

```sh
bake3 --local-env                                              # build box and the apps
bake3 run apps/oreelse --local-env                             # play in a 1280x720 window
bake3 run apps/oreelse --local-env -- --seed 42                # play the map of seed 42
bake3 run apps/oreelse --local-env -- --headless --frames 10   # headless run, exits 0
bake3 --target em --local-env apps/oreelse                     # build for the browser

python3 tools/integration.py e2e oreelse_flow oreelse_look     # the end-to-end scenarios
python3 tools/integration.py e2e oreelse_mining
python3 tools/integration.py e2e oreelse_crafting
python3 tools/integration.py visual --dir apps/oreelse/test/visual --app oreelse
```

`--seed N` seeds the map and every random choice of the game; without it the seed comes from
the time. A seeded session is reproducible: run k after the start (each restart) draws from a
seed derived from N and k, so every restart is a new map and the same again in the next
session with the same seed.

## Controls

| Key | Action |
| --- | --- |
| Any key, left or right click | Start (on the title screen) |
| W A S D | Walk north, west, south, east |
| Shift (hold) | Run, twice the walking speed |
| Right mouse button (hold) | Mine the deposit under the cursor |
| Mouse wheel | Zoom: up zooms in, down zooms out, three levels |
| ESC | One step back; with nothing left to back out of, pause or resume |
| R | Restart (on the win or lose screen) |

## Rules

### The game flow

The game opens on the title screen; any key or a left or right mouse button starts it. The key
that starts the game does nothing else: ESC on the title screen starts the game and does not
pause it.

ESC is one step back at a time, in this order:

1. Paused: resume.
2. A placement drag in flight: cancel it.
3. The inventory screen open: close it.
4. A building selected: clear the selection.
5. Otherwise: pause.

(Steps 2 to 4 come with placement, the inventory screen and the buildings.) Everything stops
dead while paused, the clock included.

R starts a new run once the game is won or lost, straight into play, on a new map. It does
nothing while playing. The camera keeps its zoom level.

The clock counts the seconds of play; the end screen shows it as the survival time.

Messages show at the bottom of the screen for 4 seconds of play, warnings for 2.5. A new message
replaces the current one.

### The map

The map is 48 by 48 tiles of 2 units, the astronaut's start at its center. Deposits of ore hold
1000 each:

- Luminite, the precious mineral, on the four tiles at the center around the start.
- Iron, copper and stone in two clusters of 3x3 tiles each, on opposite sides of the center:
  iron 4.5 to 7.5 tiles out, copper 6 to 9.75, stone 3.75 to 6.75, at angles that change with
  every map. Where two clusters overlap, the deposit generated first keeps the tile.

A hazard band runs around the playable area, just outside the edge tiles. Boulders lie scattered
over the basin and the plain, and dunes rise on the plain beyond; they change with every map and
nothing collides with them.

### The astronaut

The astronaut walks at 7 units per second and runs at 14, as fast diagonally as straight, and
faces where it walks. The camera never turns, so W is always north. It cannot leave the map.

The camera follows from behind and above at one of three zoom levels, 15, 26.3 and 38.9 units
up, looking down at 44.6, 51.8 and 59.5 degrees. It glides to a new level and follows the
astronaut smoothly, also on the title screen and while paused. The wheel over a panel of the UI
does not zoom.

### Items

| Item | Name | Detail | Recipe | Yield | Time (s) |
| --- | --- | --- | --- | --- | --- |
| Iron | Iron | Raw iron. Refines into iron plates. | mined | | 4.5 |
| Copper | Copper | Raw copper. Refines into copper plates. | mined | | 6.0 |
| Stone | Stone | Stone. Refines into stone bricks. | mined | | 1.5 |
| Luminite | Luminite | Precious mineral that can only be mined on this planet. | mined | | 24 |
| IronPlate | Iron plate | Refined iron. | Iron 2 | 1 | 0.5 |
| CopperPlate | Copper plate | Refined copper. | Copper 2 | 1 | 0.5 |
| StoneBrick | Stone brick | Sturdy building material. | Stone 2 | 1 | 0.75 |
| Wire | Wire | A wire. | CopperPlate 1 | 2 | 0.5 |
| Electronics | Electronics | An electronics circuit. | Wire 2, CopperPlate 1 | 1 | 0.5 |
| RocketEngine | Rocket engine | Puts rockets in orbit. | IronPlate 8, CopperPlate 4 | 1 | 30 |
| RocketCargoBay | Rocket cargo bay | Can store 25 luminite. Up to 4 cargo bays per rocket. | IronPlate 12, CopperPlate 6 | 1 | 30 |
| RocketLifeSupport | Rocket life support | Life support module that lets you evacuate the planet in a rocket. | IronPlate 8, CopperPlate 8, Electronics 4 | 1 | 120 |
| RocketFuelStorage | Rocket fuel storage | Rocket fuel. One per cargo bay or life support. | IronPlate 10, CopperPlate 4 | 1 | 30 |

### The inventory

The astronaut has one inventory, a pack that holds any amount of an item, but at most 15 kinds
of items. Drills and crafting deliver into it from anywhere on the map. A new kind takes the
first free one of the 15 slots and keeps it until its count drops to 0, which frees the slot;
the inventory screen shows the slots in that order. When 15 kinds are held, a new kind does not
fit and the warning `Inventory full - 15 item kinds is the limit` shows.

Every run starts with the crate of the crash: 16 iron plates and 12 copper plates.

### Mining

Hold the right mouse button over a deposit to mine it. The deposit must be within reach: the
center of its tile at most 15 units from the astronaut. A tile marker shows the target, yellow
in reach and red out of reach.

A swing takes the mining time of the ore (iron 4.5 seconds, copper 6, stone 1.5, Luminite 24);
power does not change it. When it completes, 1 of the ore goes into the inventory and 1 comes
off the deposit, and the next swing starts. A deposit that runs out is gone and its tile is
empty ground. With a full inventory a new kind of ore is not mined: the deposit keeps its ore
and the full warning shows at every swing.

Moving the cursor to another deposit starts the swing over. Releasing the button, walking out
of reach or moving the cursor off the deposit loses the progress of the swing. The right button
over a panel of the UI does not mine.

### Crafting

You order the thing you want, not its parts. Ordering an item queues the whole chain of its
prerequisites, deepest first, and you pay when you order: the chain is priced against what you
hold and its materials leave the inventory at once. If you cannot pay for the whole chain now,
the order is refused (its craft button shows dim); there is no ordering against ore you hope to
have later. Shift orders a batch of 5: one order of as many units as you can pay for, so asking
for five with materials for three gives one order of three.

Planning a unit takes every input, at every level of the chain:

1. from the order's own pool first (the surplus of its earlier crafts, such as the second wire
   of a wire craft);
2. then from the inventory: plates, wire and electronics you hold are used before new ones
   are crafted;
3. then from new crafts, `ceil(need / yield)` of them, whose inputs are planned the same way;
   the surplus yield goes to the order's pool.

Raw ore has to be in the inventory. Each order is its own block with its own pool of parts;
orders never share parts.

| Item | Raw ore of one unit |
| --- | --- |
| Electronics | 4 Copper |
| Rocket engine | 16 Iron, 8 Copper |
| Rocket cargo bay | 24 Iron, 12 Copper |
| Rocket life support | 16 Iron, 32 Copper |
| Rocket fuel storage | 20 Iron, 8 Copper |

The order book takes at most 8 orders, 8 crafted items per order, 12 item kinds held per order
(its pool, what it borrows and what it plans) and 16 queue rows in total, an order counting a
row per crafted item and at least one; recipes are planned at most 8 levels deep. An order or a
unit that would break a limit is refused.

The astronaut is a worker that crafts at speed 1, wherever it is. An idle worker takes the next
startable craft of the oldest order: the first of its items, inputs first, that has crafts left
and whose inputs for one craft are in the order's pool; the ordered item also needs room in the
inventory. The inputs leave the pool when the craft starts, and a craft takes its time divided
by the worker's speed. A finished part goes to the order's pool, the ordered item into the
inventory. When it does not fit (15 kinds held), the worker waits holding it and the order is
stalled (inventory full) until a slot frees. Once every unit is delivered, the order's leftover
pool (a spare wire, say) goes into the inventory, each kind as soon as it fits; until then the
order is wrapping up. A worker that is removed mid-craft returns the inputs of its craft to the
order's pool and loses its progress; another worker picks the craft up.

Cancel takes one unit off an order. The pool keeps what the remaining units need; the rest is
refunded into the inventory in whatever form the pool holds it (ore, plates, wire, electronics),
not counting the outputs of crafts still running. Cancelling the last unit stops the crafts in
progress for the order and refunds their inputs too, so you get back everything it took. When
the refund does not fit the 15 kinds of the inventory the cancel is refused with the warning
`No room for the refund`.

While you look at an item, its raw ore shows per ore (Iron, Copper, Stone, Luminite): the full
cost of one unit, or what is still missing when that is more, and how much of it you have, the
parts you hold counting as the ore they are made of. A row falls short only when ore is really
missing.

### Render quality

Three render quality tiers trade the look for speed; the button on the pause screen cycles them
from High to Medium to Low and back to High. The game starts on High every time it is launched,
and a restart keeps the tier.

| Tier | Resolution | Shadow map | Shadow distance | Ambient occlusion | Bloom |
| --- | --- | --- | --- | --- | --- |
| Low | half | 1024 | 91 | off | off |
| Medium | full | 2048 | 140 | off | on |
| High | full | 4096 | 140 | on | on |

## Conventions of the port

Every task of the port follows these.

- **Structure** like `apps/bunnyhopper`: `project.json` (an application using box, its
  Emscripten section embedding `../../etc` and `../../apps/oreelse/etc`),
  `include/oreelse.h` with the scene path, `src/main.c`, one header `src/game.h` with the
  reflected components (`ECS_STRUCT`/`ECS_ENUM`, `Ore` prefix) and the functions the areas
  share, `src/oreelse.c` with the module import, and one source file per area (`src/flow.c`,
  `src/map.c`, `src/player.c`, later `src/inventory.c`, `src/craft.c`, ...), each with an
  `ore_<area>_import` function. The module is `oreelse`, C functions use the `ore_` prefix.
  Assets live in `apps/oreelse/etc`, named from the repository root
  (`apps/oreelse/etc/...`).
- **Data in scripts.** Every tuning number lives in `apps/oreelse/etc/config.flecs` as
  `cfg { export const name: type = value }` with a comment, read in C with
  `ecs_const_var_get_t` (`ORE_CFG`, `ORE_CFG_I32`) where it is used, once per system run rather
  than per entity inside a loop. Items, recipes, buildings and critters are prefabs with
  reflected components in scripts (`resources.flecs`), not C tables.
- **State in reflected singletons and components**, marked modified only when a value changes,
  so the UI scripts re-evaluate only what changed: `OreClock` every frame of play, `OreBlink`
  when a signal flips, `OreToast` when a message starts and ends, `OreView` while it glides,
  `OreHover` when the cursor tile moves, `OreInventory` when an amount changes,
  `OreCraftBook` (the orders oldest first and the rows of the queue panel) when an order or a
  craft's progress changes, `OreCanOrder` on an item prefab when it flips, `OreNeeds` (the raw
  ore of the item the UI sets in it) when a row changes. The C formats no text for the UI.
- **Game state** is the exclusive pair `(OreGameState, Title | Playing | Paused | Won | Lost)`
  on its own singleton (`ore_state_is`, `ore_state_set`). Gameplay systems run only while
  `Playing`, with a query term on `ore_state_pair(world, OreGameStatePlaying)` whose source is
  `OreGameState`. That is the only pause mechanism: no time scale, no frozen flags. Only the
  camera and the render quality tier run in every state.
- **Input**: every key is a box `Action` with a `Binding` in `apps/oreelse/etc/input.flecs`.
  Actions that fire once are awaited by async loops that call an `oreelse` script function
  implemented in C (`ecs_async_function`, like `oreelse.cancel()`); held actions (walking,
  running, mining) are read from `ActionState` by C systems through the `OreActions`
  singleton. No invisible widgets for hotkeys and no synthetic input events.
- **The cancel chain** is the array `ore_cancel_steps` in `src/flow.c`: a later area inserts
  its step (a function that backs out of one thing and returns whether it did) at its place
  in the chain above.
- **UI** is a screen space UI scene like bunnyhopper's HUD, reactive to the game's
  singletons, using box's layout, text, image and widget features. Clicks are `on.click`
  handlers (or `ActionTrigger`) that call `oreelse` functions. Panels have `InputBlocker` so a
  click on them does not reach the world; labels without a panel are not hit-testable.
- **Determinism**: `OreSeed {seed, run}` holds the session seed (`--seed N` or the time) and
  the runs since the start. `ore_seed_get(world, stream)` derives the seed of a stream of the
  current run: stream 0 seeds the C generator (`OreRandom`, drawn with `ore_random01`), and
  the scripts seed their `flecs.script.math.Rng` with `oreelse.seed(stream)`, one stream per
  script (1 and 2 the deposits, 3 the ore models, 4 the scenery). A new random stream takes the next number.
- **Restart** reloads the scripts that create a run's entities (`ecs_script_reload`): every
  script that creates the entities of a run puts the `OreRunScript` tag on one of them, and
  `restart()` reloads the script of every entity with the tag (today `run.flecs` with the
  astronaut, `deposits.flecs` and `scenery.flecs`). A run's state is reset by its scripts. No static globals:
  every piece of game state lives in a component or singleton and is reset with the run.
- **The map**: tile (row, col) is centered at `x = x0 + col * tile`, `z = z0 + row * tile`.
  The conversions are `ore_tile_x`, `ore_tile_z`, `ore_col_at`, `ore_row_at` and `ore_on_grid`
  in `src/map.c`, over the `ore_grid_t` of `ore_grid_get` (read once per system run). The
  `OreMap` singleton is the tile grid: `ore_map_deposit` and `ore_map_building` give the cell
  of a tile. Entities register when they are set and clear their cell when they are removed.
  A deposit that registers on a tile another deposit holds is rejected and deleted by
  `ore_map_purge` (every frame in `OnLoad`, and by `restart()` right after its reload): an
  entity cannot be deleted in the observer of a component its script is still setting.
- **Systems write through `it->stage`** (`ecs_set`, `ecs_modified`, `ore_state_set`,
  `ore_toast`): the world is read only while the pipeline runs.
- **Tests**: end-to-end scenarios over the command port, one file per area,
  `test/integration/e2e/oreelse_<area>.py` (shrink the window, drop shadows and effects, count
  frames at a fixed 1/60 s delta, run with `--seed`), and visual scenes in
  `apps/oreelse/test/visual` with baselines. The test machines render in software at a few
  frames per second, so scenarios set up state directly (components, singletons, timers)
  instead of waiting minutes of game time, and keep their frame counts small. The commands of
  the port do not write the entities of a module, which hold the game's singletons, so the
  scenarios write those through the REST endpoints of the same port (`PUT /component/...`,
  `put` in `oreelse_flow.py`). Visual scenes include `apps/oreelse/test/visual/setup/seed`
  before the game for a fixed map.
- **Visuals** are ported from `reference/ore_else` following `reference/PORTING.md`, with the
  original's prefab and part names (`Ore`, `Astronaut`, `JetPlume`, `GroundSurface`, ...), and
  never as placeholder geometry. Animation runs in C systems that only run while `Playing`;
  visuals read the game's state instead of keeping their own copy.
- **README**: this file describes the game like a player's manual (controls, rules); each task
  adds its mechanics to it.

## Files

| File | Holds |
| --- | --- |
| `etc/oreelse.flecs` | The window, the camera, the sun, the atmosphere, the effects and the view, the render quality tier, the mining markers; includes the rest |
| `etc/config.flecs` | The constants `cfg.*` |
| `etc/resources.flecs` | The item prefabs other than the buildings, with names and recipes |
| `etc/input.flecs` | The actions, their bindings and the loops of the actions that fire once |
| `etc/world.flecs` | The ground and the basin |
| `etc/boundary.flecs` | The hazard band around the playable area |
| `etc/scenery.flecs` | The boulders and dunes of a run |
| `etc/run.flecs` | The astronaut and the singletons of a run, the order book included |
| `etc/deposits.flecs` | The deposits of a run |
| `etc/player.flecs` | The `Astronaut` template |
| `etc/props/ore.flecs` | The `Ore` template of the deposits |
| `etc/props/markers.flecs` | The `TileMarker` and `TileMarkerBad` prefabs |
| `etc/particles/jet_plume.flecs` | The `JetPlume` template of the jetpack |
| `etc/materials/terrain.flecs` | The `GroundSurface` and `BasinSurface` materials and the dirt textures |
| `etc/materials/hazard.flecs` | The hazard stripe texture and the `Hazard*` trim prefabs |
| `etc/assets/` | The original's textures (SVG), unchanged, and `terrain_dirt_orm.svg` |
| `src/flow.c` | States, clock, blink, toast, seeds, `oreelse.cancel()`, `oreelse.restart()` |
| `src/map.c` | The tile grid, the deposits' registration and the hover tile |
| `src/player.c` | Walking and the camera |
| `src/inventory.c` | The inventory and the item components |
| `src/mining.c` | Mining and its tile marker |
| `src/craft.c` | The order book, the workers, `oreelse.craft_order()`, `oreelse.craft_cancel()` |
| `src/look.c` | The render quality tiers, `oreelse.cycleQuality()` |
