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
python3 tools/integration.py e2e oreelse_mining oreelse_particles
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
  `OreHover` when the cursor tile moves, `OreInventory` when an amount changes.
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

## Critter and projectile models

The critters and projectiles are prefabs ported from the original with its names, geometry and
colors. They carry no gameplay data and stand still: OreCritters and OreWeapons give them their
components, the effect tasks drive their named parts. Each faces +Z.

### Critters

A critter's origin is between its feet on the ground. Its legs are pivots `leg_0` ... `leg_n`
at the hips, with `OreLimb {phase, swing}` for the walk (Rotation x swings); even legs are on the
left (+X), odd ones on the right, front to back. Each leg holds a `thigh`, a `shin` and a `foot`.
The parts marked glowing are emissive.

| Prefab | File | Legs | Parts |
| --- | --- | --- | --- |
| `Mite` | `etc/critters/mite.flecs` | 4 | `abdomen` (with the glowing `spark`), `thorax`, `head` (glowing `visor`, `fang_left`, `fang_right`), `antenna_left`, `antenna_right` |
| `Skitter` | `etc/critters/skitter.flecs` | 6 | `abdomen` (`ridge`), `thorax` (`plate`), `head` (glowing `eye_left`, `eye_right`, `mandible_left`, `mandible_right`), the glowing `stinger` |
| `Brute` | `etc/critters/brute.flecs` | 6 | `abdomen` (`spike_a`, `spike_b`), `thorax` (`carapace`), `head` (`crest`, glowing `eye_left`, `eye_right`, `mandible_left`, `mandible_right`, `tusk_left`, `tusk_right`) |
| `Behemoth` | `etc/critters/behemoth.flecs` | 8 | `abdomen` (glowing `spot_a`, `spot_b`, `spot_c`, and `spine_left`, `spine_right`, `spine_mid`), `thorax` (glowing `flank_left`, `flank_right`), the back plates `plate_0` ... `plate_3`, `head` (`crown`, glowing `eye_0` ... `eye_4`, glowing `brow`, `mandible_left`, `mandible_right`, `tusk_left`, `tusk_right`, `horn_left`, `horn_right`) |

The behemoth's back plates are pivots with `OreLimb` like the legs (a swing of 0.05), each with
a `shell`, a `ridge`, a glowing `seam`, `barb_left` and `barb_right`. Its tusks and horns are
turned in two axes; their angles are converted to box's Euler order (`reference/PORTING.md`).

### Projectiles

A projectile's origin is its center.

| Prefab | File | Parts |
| --- | --- | --- |
| `Tracer` | `etc/projectiles/tracer.flecs` | `core`, a glowing gold streak with a point light (2.6, range 4.5) |
| `Missile` | `etc/projectiles/missile.flecs` | `body`, `tip`, `fin_left`, `fin_right`, the glowing `flame` with a point light (3, range 6), and `trail`, the point the smoke trail comes out of |
| `LaserBeam` | `etc/projectiles/laser_beam.flecs` | One glowing pink box, 1 unit long along Z: the laser turret places an instance halfway between its muzzle and the target, turned toward the target and scaled along Z to their distance |

## Building models

The buildings are prefabs in `etc/buildings/`, one file per building, with the original's names
and geometry, materials and lights. They stand still: the gameplay tasks add their components
(item, recipe, footprint, health, power) to the same prefabs, and the animation and effect tasks
drive the named parts below. Every building's origin is the center of its footprint on the
ground (the center of its tile, the corner between the four tiles of a 2x2 building, the center
of the middle tile of a 3x3 one), and it faces +Z. The names are those of the prefab's parts
(`ecs_lookup_from(world, Drill, "carriage.slider.bit")`): the parts of an instance are unnamed
children, each an instance (`IsA`) of the prefab's part of the same path, so a system finds the
part of an instance as the child of its parent whose `IsA` target is the prefab's part, the way
the original's `oreChild` does. The parts have no `DynamicTransform`, so a system that moves one every frame adds it.

| Prefab | File | Named parts |
| --- | --- | --- |
| `Drill` | `drill.flecs` | `carriage` (the piston that rides the legs, rest y 1.48, travel 0.26), `carriage/slider` (rides fore and aft, rest z 0.12, travel 0.24), `carriage/slider/bit` (spins about y and shakes), `crown/pod_l/lamp_l`, `crown/pod_r/lamp_r` (status lamps), `dust` (the drill dust) |
| `SolarPanel` | `solar_panel.flecs` | `junction/light` (indicator light) |
| `Wall` | `wall.flecs` | `lamp` (hazard lamp) |
| `WallSegment` | `wall_segment.flecs` | The connector between two walls on neighboring tiles: placed on the middle of their shared edge, turned by PI / 2 about y between two walls of one column |
| `GunTurret` | `gun_turret.flecs` | `head` (turns about y to aim), `head/fx` (the muzzle: flash and tracers), `head/sight` (status lamp) |
| `LaserTurret` | `laser_turret.flecs` | `head` (turns about y to aim), `head/fx` (the lens: beam start and glow), `hit_fx` (the beam's sparks where it hits), `column/lamp` (status lamp) |
| `MissileTurret` | `missile_turret.flecs` | `head` (turns about y to aim), `head/fx` (the launch above the pods), `pillar/light` (status lamp) |
| `PadBeacon` | `pad_beacon.flecs` | A template, the corner beacon of the rocket pad: `lamp` (amber lamp) |
| `Factory` | `factory.flecs` | 2x2. `ram` (the press, moves along y, rest y 1.72, travel 0.62), `roller_a` to `roller_d` (each with its `drum`, which spins about its own y; the rollers lie along x), `wheel_left/flywheel`, `wheel_right/flywheel` (spin about y), `puff_l`, `puff_r` (the tops of the stacks, the smoke puffs), `hall/steam_l`, `hall/steam_c`, `hall/steam_r` (the vents of the grille, the steam), `hot_plate` (emissive 0.06 at rest, 2.6 while working), `work_glow` (emissive 0.08 at rest, 1.8 while working, and its `PointLight` of range 6.5 at intensity 0, 5.5 while working), `beacon_l`, `beacon_r` (work lamps, blink 2), `crown/status_l`, `crown/status_r` (status lamps) |
| `DronePad` | `drone_pad.flecs` | 2x2. `bay_a` to `bay_d` (the landing bays, on the corners at x and z of +-0.99) with their `lamp` (status lamps), `column/head/beacon_l`, `column/head/beacon_r` (status lamps), `column/cabinet/readout`, `column/mast/tip` (lamps always on). Drone b of its 4 docks at the angle (b + 0.5) * 2 PI / 4, at (sin, cos) * 1.4 from the center and 0.62 above the ground |
| `RepairDrone` | `drone.flecs` | The drone of the pad, its origin the center of the hull: `rotor_a` to `rotor_d` (spin about y), `hull/eye` (lamp), `weld` (the nozzle, the weld sparks). Its rotors share the `RotorBlade` prefab |
| `RocketPad` | `rocket_pad.flecs` | 3x3. `clamp_a` to `clamp_d` (`PadClamp` templates: the hold-down clamps, which swing out by their `tilt` at the launch), `tower/beacon` (lamp), `beacon_b`, `beacon_c`, `beacon_d` (`PadBeacon`). The rocket stands on its axis at y 0.74, the original's `LaunchPad.base_y` |
| `PadClamp` | `rocket/pad_clamp.flecs` | A template, the clamp: props `yaw` (faces the rocket), `tx`, `tz` (which way it swings) and `tilt` (0 at rest; the original's `cfg.clampTilt` 0.6 at the launch). The original turns it by (tx * tilt, yaw, tz * tilt) in flecs-engine's X Y Z order; the template converts that to box's Y X Z order |
| `Rocket` | `rocket/rocket.flecs` | A template, the rocket on the pad (see below) |

The rocket is an entity with the `Rocket` template, its origin the bottom of the stack on its
axis, which OreRocket places on the pad and keeps in step with its rocket state by setting the
props of its `Rocket` component; the template rebuilds the stack in the script whenever they
change, so nothing in C builds or deletes its parts:

| Prop | Type | Builds |
| --- | --- | --- |
| `engines` | i32 | The engine skirt and four fins when above 0, and an engine bell (`RocketBell`, `rocket/rocket_bell.flecs`) per engine: up to 4 on a ring of radius 0.22, above 4 on a ring of 0.32 at 0.72 of their size, above 6 a third of them on an inner ring of 0.13, above 8 at 0.56 of their size |
| `fuel` | i32 | A fuel tank of height 1.02 per fuel storage, above the skirt |
| `cargo` | i32 | A cargo bay of height 0.78 per bay (`RocketBay`, `rocket/rocket_bay.flecs`, with four port lights, `RocketPort`, `rocket/rocket_port.flecs`), above the tanks, each turned 0.5 more about y |
| `life_support` | i32 | The capsule with its window and hatch when above 0, above the bays |
| `loaded` | bool | Lights the port lights of the bays (the rocket holds Luminite) |
| `valid` | bool | The nose cone on top (the rocket can launch: `enginesPerUnit` engines and one fuel storage per unit, a unit being a cargo bay or the life support) |

The lamps are instances of the `Light` template (`etc/props/light.flecs`): a `housing`, a
`bulb` and a `glass` cover, its origin the bottom of the housing, facing +Y (a lamp on a wall
is turned by PI / 2 about x). The `bulb` is the part the game drives: it has the `Rgb` and the
`Emissive` of the lamp's color at `strength` while `on`, and a `PointLight` of intensity
`light` and `range` when `light` is above 0. The props say how: a status lamp (`status: true`)
is the power lamp of its building, which OrePower colors `color` (live), `ok`, `warn` or `dead`
and blinks at `blink` per second while dead; a work lamp (`work: true`) lights while its
building works. The props are on the `Light` component of the lamp entity (`lamp_l`, `sight`,
...). The template builds the parts on every lamp of an instance, not on the prefab's, so they
keep their names there: the bulb is the lamp's child `bulb`.

The shared materials are inherited prefabs that a part derives from and resizes with its own
`Box`: `HazardTag`, `HazardTrim`, `HazardTrimShort`, `HazardBorderShort`, `HazardBorderLong`
and `HazardBoundary` (`etc/materials/hazard.flecs`), `SolarCells`, `AccessPlate`, `VentGrille`,
`FanDisc` and `BayPad`. `TileMarker` and `TileMarkerBad` (`etc/props/markers.flecs`) are the
glowing plates mining and placement show over a tile, instances placed at y 0.14.

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
| `etc/run.flecs` | The astronaut and the singletons of a run |
| `etc/deposits.flecs` | The deposits of a run |
| `etc/player.flecs` | The `Astronaut` template |
| `etc/props/ore.flecs` | The `Ore` template of the deposits |
| `etc/critters/` | The `Mite`, `Skitter`, `Brute` and `Behemoth` prefabs |
| `etc/projectiles/` | The `Tracer`, `Missile` and `LaserBeam` prefabs |
| `etc/props/markers.flecs` | The `TileMarker` and `TileMarkerBad` prefabs |
| `etc/particles.flecs` | Includes the particle effects |
| `etc/particles/pools.flecs` | The particle sprites, how the original's pools map to the layers, the effect budgets (`ParticleSettings`) |
| `etc/particles/*.flecs` | The particle effects, one file per effect group of the original: one shot prefabs (`BoomBurst`, `CombustBurst`, `MissileLaunch`, `FootDust`, ...) and emitter templates (`JetPlume`, `DrillDust`, `BeamGlow`, `MuzzleFlash`, ...) |
| `etc/materials/terrain.flecs` | The `GroundSurface` and `BasinSurface` materials and the dirt textures |
| `etc/materials/hazard.flecs` | The hazard stripe texture and the `Hazard*` trim prefabs |
| `etc/materials/solar_cells.flecs` | The solar cell texture and `SolarCells` |
| `etc/materials/wall_plate.flecs` | The wall plate textures and `AccessPlate` |
| `etc/materials/vent.flecs` | The vent grille textures and `VentGrille` |
| `etc/materials/fan.flecs` | The fan blade textures and `FanDisc` |
| `etc/materials/drone_bay.flecs` | The drone bay textures and `BayPad` |
| `etc/props/light.flecs` | The `Light` template of the lamps |
| `etc/buildings/` | The building prefabs, one file per building, and the drone (see Building models) |
| `etc/rocket/` | The `Rocket` template and its parts `RocketBell`, `RocketBay` and `RocketPort`, and the `PadClamp` template of the rocket pad |
| `etc/assets/` | The original's textures (SVG), unchanged, and `terrain_dirt_orm.svg` |
| `src/flow.c` | States, clock, blink, toast, seeds, `oreelse.cancel()`, `oreelse.restart()` |
| `src/map.c` | The tile grid, the deposits' registration and the hover tile |
| `src/player.c` | Walking and the camera |
| `src/inventory.c` | The inventory and the item components |
| `src/mining.c` | Mining and its tile marker |
| `src/look.c` | The render quality tiers, `oreelse.cycleQuality()` |
| `src/particles.c` | The helpers that play the effects (`ore_burst`, `ore_burst_aimed`, `ore_explosion`, `ore_combust`), the flash of their lights, `oreelse.explosion()`, `oreelse.combust()` |
