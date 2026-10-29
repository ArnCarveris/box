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
python3 tools/integration.py e2e oreelse_crafting oreelse_buildings oreelse_rocket
python3 tools/integration.py e2e oreelse_power oreelse_critters oreelse_critter_fx
python3 tools/integration.py e2e oreelse_player_fx oreelse_hud oreelse_building_animation
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
| Shift (hold) | Run, twice the walking speed; at the release of a placement drag, fill the rectangle |
| Right mouse button (hold) | Mine the deposit under the cursor, or demolish the building there; a press cancels a placement drag |
| Left mouse button | Place the selected building; drag for a line, an L or a rectangle |
| 1, 2, 3, 4, 5, 6, 8, 9, 0 | Select the wall, drill, solar panel, gun turret, laser turret, missile turret, rocket pad, drone pad, factory (again: clear the selection) |
| Q | Pipette: select the building under the cursor, the drill over a deposit |
| T, Y, U, I | Install a rocket engine, fuel storage, cargo bay, life support on the rocket |
| O | Load the Luminite you carry into the rocket |
| P | Launch the rocket |
| F, G, H, J | Order a rocket engine, fuel storage, cargo bay, life support; with Shift five |
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

(Step 3 comes with the inventory screen.) Everything stops dead while paused, the clock
included.

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

Walking, the astronaut swings its legs and its arms, each arm with the opposite leg, bobs up to
0.07 units with every step and kicks up a puff of dust at every half step, twice as often while
running. Standing, it holds its limbs straight.

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

The buildings are items too, see below.

### The inventory

The astronaut has one inventory, a pack that holds any amount of an item, but at most 15 kinds
of items. Drills and crafting deliver into it from anywhere on the map. A new kind takes the
first free one of the 15 slots and keeps it until its count drops to 0, which frees the slot;
the inventory screen shows the slots in that order. When 15 kinds are held, a new kind does not
fit and the warning `Inventory full - 15 item kinds is the limit` shows.

Every run starts with the crate of the crash: 16 iron plates, 12 copper plates and a solar
panel.

### Mining

Hold the right mouse button over a deposit to mine it. The deposit must be within reach: the
center of its tile at most 15 units from the astronaut. A tile marker shows the target, yellow
in reach and red out of reach.

A swing takes the mining time of the ore (iron 4.5 seconds, copper 6, stone 1.5, Luminite 24);
power does not change it. When it completes, 1 of the ore goes into the inventory and 1 comes
off the deposit, and the next swing starts. A deposit that runs out is gone and its tile is
empty ground. With a full inventory a new kind of ore is not mined: the deposit keeps its ore
and the full warning shows at every swing.

Every ore mined bursts off the deposit, and a deposit shrinks as it runs out: to
`0.55 + 0.45 * amount / 1000` of its size, so a deposit about to run out is a little over half
its size. A deposit used up goes in a puff of dust. The Luminite deposits glint.

Moving the cursor to another deposit starts the swing over. Releasing the button, walking out
of reach or moving the cursor off the deposit loses the progress of the swing. The right button
over a panel of the UI does not mine. Over a building the right button demolishes it instead
(see below).

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

### Buildings

Every building is an item: it is crafted into the inventory through the order book like any
other item (yield 1) and placed by spending one from the inventory.

| Building | Name | Detail | Recipe | Time (s) | Footprint | HP | Power | Category |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Wall | Wall | Buys your base a bit more time. | StoneBrick 4 | 1 | 1x1 | 300 | | |
| Drill | Drill | Mines ore deposits. | IronPlate 6 | 1 | 1x1, deposit tiles only | 100 | 4 W | Mining |
| SolarPanel | Solar panel | Generates power. | IronPlate 4, CopperPlate 6 | 2 | 1x1 | 100 | +20 W | |
| GunTurret | Gun turret | Short range rapid fire. | IronPlate 10, Wire 15, Electronics 5 | 2 | 1x1 | 120 | 0 W | Weapons |
| LaserTurret | Laser turret | High efficiency photon delivery for melting opponents. | IronPlate 30, Wire 10, Electronics 10 | 4 | 1x1 | 150 | 25 W | Weapons |
| MissileTurret | Missile turret | Firepower to deal with the most sturdy targets. | IronPlate 40, Wire 20, Electronics 20 | 5 | 1x1 | 180 | 20 W | Weapons |
| Factory | Factory | Speeds up crafting. | IronPlate 20, Wire 20, Electronics 5, StoneBrick 10 | 12 | 2x2 | 100 | 75 W | Construction |
| RocketPad | Rocket pad | Rocket launching facility for luminite shipments and evacuation. | IronPlate 25, CopperPlate 50, StoneBrick 25, Wire 50, Electronics 25 | 8 | 3x3, at most 1 | 400 | | |
| DronePad | Drone pad | Drones repair buildings, repairs cost iron. | IronPlate 24, Wire 18, Electronics 12 | 5 | 2x2 | 140 | 20 W | |

A building covers its footprint from its top left tile (the lowest row and column) on, and
stands at the center of it. A tile holds one building. Two walls on orthogonally neighboring
tiles are joined by a wall segment, which is only for the look.

### Placement

The digit keys select a building for placement (with Shift too): 1 the wall, 2 the drill, 3 the
solar panel, 4 the gun turret, 5 the laser turret, 6 the missile turret, 8 the rocket pad, 9 the
drone pad and 0 the factory. The key of the selected building clears the selection; the key of a
building you have none of does nothing. Q, the pipette, selects the building under the cursor,
the drill over a deposit, and clears the selection over empty ground or off the map; with none
of that building in the inventory it warns `No Drill in inventory` (with the building's name)
and keeps the selection.

A building can be placed on a cell when every tile of its footprint is on the map without a
building, every tile has a deposit for a drill and none has one for any other building, the
nearest of its tile centers is at most 15 units from the astronaut, its limit is not reached (one
rocket pad) and the inventory holds one. Critters and the astronaut do not block it.

A left click places the selected building on the tile under the cursor, instantly, spending one.
A drag places nothing until the button is released; then the cells step by the footprint from
the tile the drag started on to the tile under the cursor: a line along a row or a column, and
otherwise an L whose corner follows where the cursor sits in the last tile. Holding Shift at the
release fills the rectangle instead. A drag covers at most 64 cells. The cells are placed row by
row from the top left, one item each, until the item runs out (so the top left part gets built),
skipping the cells that cannot be placed. When nothing was placed and a cell was too far away,
the warning `Too far away` shows. When the last one is placed the selection clears. Every
building placed comes down in a puff of dust. ESC or a
right click during a drag throws it away (`Placement cancelled`).

While a building is selected, a preview shows on the cells the click or the drag would place: the
tile markers under every tile of a cell, yellow where it can be placed (counting the items the
cells before it use) and red where it cannot, and a translucent ghost of the building, blue or
red.

### Demolish

The right mouse button over a building demolishes it instead of mining the tile: the swing takes
0.8 seconds for every building, with the reach and the marker of mining, and moving between the
tiles of one building does not restart it. An undamaged building goes back into the inventory
(refused with the full warning when it is a new kind and 15 kinds are held). A damaged one gives
back the salvage of its recipe instead: `trunc(amount * health / max)` of every input, so a wall
at half health gives 2 bricks and a gun turret at 10% 1 iron plate and 1 wire; it shows as
`Salvaged 1 Iron plate, 1 Wire (10%)` (or `Salvaged nothing (5%)`), and is refused with
`Inventory full - can't salvage` when the new kinds do not fit. Then the building is gone; a
deposit under it (a drill's) is mined by the next swing. A building demolished goes in a puff
of dust. A building whose health drops to 0 is
destroyed.

### Power

Solar panels produce power and the other powered buildings consume it, whether they work or
not: an idle drill on a depleted tile and an idle factory draw as much as busy ones.

| Building | Watts | Category |
| --- | --- | --- |
| Solar panel | +20 | |
| Drill | 4 | Mining |
| Gun turret | 0 | Weapons |
| Laser turret | 25 | Weapons |
| Missile turret | 20 | Weapons |
| Factory | 75 | Construction |
| Drone pad | 20 | |

The power panel splits 6 bars between three categories, Mining, Weapons and Construction, each
at a level of 0 to 4; every run starts at 2, 2 and 2. A category runs at half its level: 0, 0.5,
1, 1.5 or 2 times the normal speed, and at level 0 it stops. A building without a category (the
drone pad) runs at 1.

The production is the sum of the panels' watts. The demand is the sum of the consumers' watts
times the bars assigned out of 6, one scale for every consumer, the drone pads included: with
every category at 1 the base draws half its watts, with all 6 bars assigned the full watts.

When there is demand and the production is below it, the base is in a **blackout**, all or
nothing. In a blackout the drills, the factories, the laser and missile turrets and the drones
stop, keeping their progress, and resume when the power is back. The gun turrets (scaled by
Weapons), the astronaut's crafting and mining, the pistol and the rockets keep working.

Clicking bar n (1 to 4) of a category's column sets the category to level n, or, when it is at
n already (the topmost lit bar), steps it down to n - 1, so a click on the single lit bar of a
category switches it off. An increase is capped by the free bars; when there is none to take,
the warning `Only 6 power bars - free one somewhere else` shows and nothing changes.

A drill on a deposit mines without a blackout and with Mining above 0: every mining time of its
ore (iron 4.5 seconds, copper 6, stone 1.5, Luminite 24) at multiplier 1, so every 0.75 seconds
of stone at level 4, it moves 1 of the ore into the inventory. When the ore does not fit (a new
kind with 15 kinds held) nothing is mined, the deposit keeps it and the interval is used up.
Once its deposit is gone the drill stands idle, still drawing power.

A factory is a worker of the order book that crafts at the Construction multiplier, so two
crafts of an order go twice as fast with one factory at level 2. In a blackout or at level 0 it
holds its craft and goes on with it when the power is back. A factory demolished or destroyed
mid-craft returns the inputs of its craft to the order. The astronaut crafts at speed 1 whatever
the power.

Every powered building shows its state on its status lamps:

| State | Lamp |
| --- | --- |
| Blackout (a consumer) or its category at 0 | Red {248, 84, 72}, blinking |
| Its category at 1 | Orange {240, 152, 72} |
| Its category at 2 | Green {124, 226, 148} |
| Its category at 3 or 4, or no category | Cyan {92, 236, 222} |

A gun turret draws no power, so a blackout does not kill its lamps; Weapons at 0 does. A lit
status lamp also casts a little light of its color around it, which blinks with it.

### Buildings at work

A building works at its work rate: 0 for a consumer in a blackout, for a drill that does not
mine (no deposit left under it, or Mining at 0) and for a factory whose crafter is idle (nothing
in the queue it can start), otherwise the multiplier of its category (0.5 to 2, 1 without a
category). While it works it moves at that rate, and at 0 it stops where it is; while the game
is paused everything stands still.

- The factory turns the drums of its four rollers and its two flywheels (5 radians a second),
  strokes its press (6 radians of the stroke a second, 0.62 down from its rest) with a puff of
  dust and sparks where it lands, puffs smoke out of its stacks every 0.5 and 0.62 seconds and
  steams from its three vents. Its hot plate and its work glow light up (the glow lights the
  press, brighter as the press comes down) and fade within about half a second when the work
  stops; its beacons blink twice a second (times the rate). A blackout puts them out at once.
- The drill turns its bit (7 radians a second) and shakes it, rides its carriage up and down
  the legs and its slider fore and aft, and throws dust; idle, its bit settles back at rest.

### Critters

| Critter | HP | Speed | Damage/s | Attack range | Radius | Wave cost | From wave |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Mite | 12 | 6.8 | 4 | 1.1 | 0.34 | 1 | 1 |
| Skitter | 50 | 5.2 | 8 | 1.4 | 0.56 | 3 | 4 |
| Brute | 100 | 2.4 | 50 | 1.9 | 1.1 | 15 | 7 |
| Behemoth | 1400 | 1.2 | 100 | 3.2 | 2.2 | 120 | 13 |

A critter goes for the astronaut when it is within 14 units, unless a building (measured to its
center) is closer; with the astronaut farther away it goes for the nearest building anywhere,
and with no building on the map for the center of the map. It looks again every 0.375 to
0.625 seconds, and every moment it has no target. Within its attack range of the target's
center it chews it: the target loses the critter's damage per second.

There is no pathfinding and buildings do not block critters: walls are targets, not obstacles.
A critter walks straight at its goal, a little off to the side by a heading of its own (up to
0.22 radians), and the building on the tile 1.2 units ahead of it becomes its target, so it
chews through whatever stands in its way.

Critters do not stack: overlapping critters push each other apart, the light ones more than
the heavy ones (a critter weighs its radius squared, so a behemoth shoves mites aside and barely
moves). They stay on the map.

A critter at 0 HP dies and counts as a kill of the run: it bursts into green flame (the combust,
as large as the critter) and leaves its corpse. A building at 0 HP is destroyed.

A corpse is no longer a critter: it does not move, attack or push, and turrets do not shoot at
it. It lies tipped over, charred black with its eyes gone dark and embers smoldering
on it, for 4 seconds, then sinks into the ground at 0.7 units a second and is gone. At most 100
corpses lie on the map; the 101st takes the place of the oldest.

Critters walk with a gait of their own: each step bobs the body up and down and it waddles from
side to side, its legs swinging in turn. A critter that chews stands on the ground and only
twitches its legs.

### Waves

The first wave comes 90 seconds into the run, every next one 50 seconds after the last critter
of the previous wave spawned. The waves never end and grow by 30% each: wave w has a budget of
`2 * 1.3^(w - 1)` (2 for wave 1, 4.39 for wave 4, 9.65 for wave 7, 16.3 for wave 9, 46.6 for
wave 13, 133 for wave 17, 4031 for wave 30).

Every wave has a style, drawn per wave, that sets the share of the budget of every critter:

| Style | Chance | Mite | Skitter | Brute | Behemoth |
| --- | --- | --- | --- | --- | --- |
| Swarm | 25% | 0.7 | 0.2 | 0.1 | 0 |
| Elite | 25% | 0.05 | 0.15 | 0.55 | 0.25 |
| Mixed | 50% | 0.25 | 0.25 | 0.25 | 0.25 |

The wave buys its critters one at a time, each picked at random with the weight share / cost
among the critters its number unlocked that the budget left can pay for (and the most
expensive affordable one when none of them has a weight), until the budget runs out. A wave
holds at most 10000 critters and 25 behemoths; near 10000 critters it buys only what still lets
it spend the rest, so a wave too rich for 10000 critters takes 25 behemoths whatever its style.
So wave 1 is two mites, and although brutes
come from wave 7 and behemoths from wave 13, the first brute can be afforded in wave 9 and the
first behemoth in wave 17.

The critters of a wave spawn one by one, the first at once, then one every 0.45 seconds, faster
for big waves so that a wave takes at most 20 seconds to spawn. Each spawns on a side of the map
other than the previous critter's (north, south, west or east), at a random place along it a
tile inside the edge, up to 5 units off on both axes.

### Health

The astronaut has 100 HP. After 5 seconds without damage it regenerates 2 HP a second up to
the full 100. At 0 HP it falls over in a splat and the game is lost.

### Rockets

The score is the Luminite shipped to orbit. A cargo rocket ships it; a rocket with life support
ships it and takes you off the planet, which wins the game. Shipped Luminite is banked even if
you die later.

The rocket is built on the rocket pad (one on the map) from the rocket parts in the inventory. A
unit is a cargo bay or the life support: a rocket takes up to 4 cargo bays and 1 life support,
and needs 4 engines and 1 fuel storage per unit. It is valid, ready to launch, with at least
one unit, exactly 4 engines per unit and exactly one fuel storage per unit. A cargo bay holds 25
Luminite, so a rocket ships at most 100. A rocket with only the life support (4 engines, 1 fuel
storage) is valid, ships nothing and wins.

Everything below works only while playing with a pad and the rocket on it, not in flight, and
with or without a panel open:

- T, Y, U and I install an engine, a fuel storage, a cargo bay and the life support, spending
  one from the inventory. An engine only goes on while there are fewer than 4 per unit, a fuel
  storage while there are fewer than the units; otherwise the warning
  `Engines full for this payload - add a cargo bay (U) first` (or
  `Fuel tanks full for this payload - add a cargo bay (U) first`) shows. A fifth cargo bay or a
  second life support does not go on.
- The minus buttons of the rocket panel uninstall a part into the inventory. The engines and
  fuel storages above the limits of the units left and the Luminite above the capacity left come
  back with it, shown as `Payload removed - returned 4 engines, 1 fuel tank, 25 Luminite` (only
  what came back besides the part). When the returned kinds do not fit the inventory nothing
  happens and the warning `No room in the inventory for the parts` shows.
- O loads the Luminite you carry into the cargo bays, as much as they have room for.
- P launches a valid rocket. It loads first, its engines fire for 1.6 seconds, then it climbs,
  its speed growing by 9 units a second every second, until it is 60 units up, about 5.25
  seconds after the launch. With life support you board it: the astronaut stands on the pad,
  hidden, and cannot move, mine, shoot or regenerate, and the swarm can neither hurt nor target
  it.

When the rocket arrives its Luminite is added to the shipped total. With life support the game
is won; a cargo rocket shows `Shipment away!  +25 Luminite` and leaves the pad empty for the
next rocket.

A pad that is demolished or destroyed while the rocket stands on it loses every part on it and
the Luminite loaded. A rocket in flight flies on.

F, G, H and J order a rocket engine, fuel storage, cargo bay and life support through the order
book, one unit, or one order of five with Shift (as many as you can pay for), whatever screen is
open.

### The jetpack

Over a tile with a building the astronaut hovers on its jetpack 3 units up, rising at 7 units a
second and coming back down at 5 once it is off the buildings. The jet plume fires while it is
in the air and stops when it lands; its limbs trail behind it while it flies and dangle while it
hovers in place.

### The HUD

The HUD shows over the world outside the title screen. In the top right corner the status bar
holds four gauges, left to right:

- **Health**: the heart and a bar of the astronaut's HP, green, amber below 60% and red below
  30%.
- **Luminite**: `held / shipped`, the Luminite in the inventory and the Luminite shipped to
  orbit.
- **Waves**: the critter and a bar of the time to the next wave, filling up as it nears: purple,
  orange once it is three quarters there, and full, blinking red, while a wave spawns.
- **Power**: `production / demand W` (rounded down) and a bar of the ratio of the two, green, red
  in a blackout.

While playing or paused the bottom of the screen shows, from the top down: the message of the
toast (red for a warning, teal otherwise), the progress bar of a swing at a deposit or a
building while one is under way, and with a rocket pad the status of its rocket: `LAUNCHING`
once it is launched, `Rocket ready to launch (P)` (blinking green) when it is valid, else what
it needs: `Install a cargo bay (U) or life support (I)`, `Needs 4 engines, 2 installed (T)` or
`Needs 1 fuel tanks, 0 installed (Y)`. Below 30% health `! SUIT BREACH !` blinks red at the top
of the screen.

Clicks, the right button and the wheel over a panel of the UI (the status bar) do not reach the
world: they place, mine and zoom nothing. The plain lines of text let them through.

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
  ore of the item the UI sets in it) when a row changes, `OrePowerAlloc` (the levels of the
  power categories) when a level changes, `OrePowerState` (production, demand, ratio and
  blackout) when a value changes, `OreRocketState` (the rocket, the score, the Luminite held
  and what can be done to the rocket) when a value changes, every frame of a flight. The C
  formats no text for the UI.
- **Game state** is the exclusive pair `(OreGameState, Title | Playing | Paused | Won | Lost)`
  on its own singleton (`ore_state_is`, `ore_state_set`). Gameplay systems run only while
  `Playing`, with a query term on `ore_state_pair(world, OreGameStatePlaying)` whose source is
  `OreGameState`. That is the only pause mechanism: no time scale, no frozen flags. Only the
  camera and the render quality tier run in every state; outside play, systems only hide what
  belongs to play (the tile markers, the placement preview) and pause the effects that play
  (the jet plume, the glints of the Luminite).
- **Input**: every key is a box `Action` with a `Binding` in `apps/oreelse/etc/input.flecs`.
  Actions that fire once are awaited by async loops that call an `oreelse` script function
  implemented in C (`ecs_async_function`, like `oreelse.cancel()`); held actions (walking,
  running, mining) are read from `ActionState` by C systems through the `OreActions`
  singleton. No invisible widgets for hotkeys and no synthetic input events.
- **The cancel chain** is the array `ore_cancel_steps` in `src/flow.c`: a later area inserts
  its step (a function that backs out of one thing and returns whether it did) at its place
  in the chain above (placement's are `ore_placement_cancel_drag` and
  `ore_placement_deselect`).
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
colors. The critters carry their gameplay data (`OreHealth`, `OreSpeed`, `OreDamage`,
`OreRange`, `OreRadius`, `OreWaveTier`), their gait (`OreGait {step, bob}`) and the effect they
die in (`OreDeathBurst {burst}`, `CombustBurst` for all four), and the waves give their
instances `OreCritter`, `OreTarget` and `OreVelocity`; OreWeapons gives the projectiles theirs,
the effect tasks drive the named parts. Each faces +Z.

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

The animation (`src/critter_fx.c`) reads the critters' state. `OreCritter.anim`, the walk cycle,
starts at U(0, 2 PI) and advances by `dt * speed * step` while the critter walks (not while it
attacks). `OreCritterFx` sets the critter's `Position.y` to `|sin(anim)| * bob` (0 while it
attacks) and its `Rotation.z` to `sin(anim / 2) * 0.07`; `OreCritterMove` turns it along its
heading. `OreCritterLimbs` sets the `Rotation.x` of every `OreLimb` below a critter to
`sin(anim + phase) * swing`, times 0.35 while it attacks.

A dead critter's corpse is a new entity, an instance of the critter's prefab under the critters'
parent with `OreCorpse {age, depth}` and no `OreCritter`, so none of the critter systems, the
spatial index or the turrets see it. It lies at the critter's place on the ground, turned by
(1.1 + U(0, 0.2), heading, (U(0, 1) - 0.5) * 0.8) in the original's Euler order (converted to
box's), scaled (1, 0.55, 1), with the `Tint` `cfg.corpseColor` on its root; `OreCorpseChar`
turns off the emission of the parts it inherits from the prefab (box does not tint emission),
and a child instance of the `CorpseEmbers` template sits r / 2 above its origin, scaled 2 r, at a
random yaw. `OreCorpses` ages it, sinks it after `cfg.corpseTime` and deletes it at 2 r + 1
below the ground, and deletes the oldest above `cfg.corpseCap`. `oreelse.corpse(prefab,
position, yaw)` lays one from a script.

### Projectiles

A projectile's origin is its center.

| Prefab | File | Parts |
| --- | --- | --- |
| `Tracer` | `etc/projectiles/tracer.flecs` | `core`, a glowing gold streak with a point light (2.6, range 4.5) |
| `Missile` | `etc/projectiles/missile.flecs` | `body`, `tip`, `fin_left`, `fin_right`, the glowing `flame` with a point light (3, range 6), and `trail`, the point the smoke trail comes out of |
| `LaserBeam` | `etc/projectiles/laser_beam.flecs` | One glowing pink box, 1 unit long along Z: the laser turret places an instance halfway between its muzzle and the target, turned toward the target and scaled along Z to their distance |

## Building models

The buildings are prefabs in `etc/buildings/`, one file per building, with the original's names
and geometry, materials and lights. The gameplay tasks add their components (item, recipe,
footprint, health, power) to the same prefabs, and the animation and effect tasks drive the named
parts below. A model that is not a placed building (no `OreBuilding` above it, a ghost) stands
still. Every building's origin is the center of its footprint on the
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
axis, which `launch.flecs` places on the pad and keeps in step with the rocket state
(`OreRocketState`) by setting the props of its `Rocket` component; the template rebuilds the
stack in the script whenever they change, so nothing in C builds or deletes its parts. It rises
with the state's height during the ascent. The entity always exists; without parts (no pad, an
empty pad, after the arrival) the template builds nothing, so the rocket is gone:

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
and blinks with the lamp signal of `OreBlink` while dead (the template gives its bulb an
`OreStatusLamp` with those colors and a `PointLight` of `cfg.statusLampLight` and
`cfg.statusLampRange`, small for box's falloff, which blinks with it); a work lamp
(`work: true`) lights while its building works, blinking at `blink` times the work rate (the
template gives its bulb an `OreWorkLight`). The props are on the `Light` component of the lamp entity (`lamp_l`, `sight`,
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
| `etc/oreelse.flecs` | The window, the camera, the sun, the atmosphere, the effects and the view, the render quality tier, the mining markers, the critter tiers and their death effects (`OreArsenal`), the one shot effects the C plays (`OreEffects`), the HUD; includes the rest |
| `etc/config.flecs` | The constants `cfg.*` |
| `etc/resources.flecs` | The item prefabs other than the buildings, with names and recipes |
| `etc/buildings/*.flecs` | The building prefabs, models with their item, recipe, footprint, health and power |
| `etc/buildings/rules.flecs` | The connector rules (`OreConnectRule`): the wall segment between two walls |
| `etc/input.flecs` | The actions, their bindings and the loops of the actions that fire once |
| `etc/world.flecs` | The ground and the basin |
| `etc/boundary.flecs` | The hazard band around the playable area |
| `etc/scenery.flecs` | The boulders and dunes of a run |
| `etc/run.flecs` | The astronaut, the parents of the placed buildings and of the critters, and the singletons of a run, the order book, the selection, the power allocation, the rocket state and the waves included |
| `etc/launch.flecs` | The items of a rocket (`OreRocketKit`) and the rocket on the pad, a `Rocket` instance that follows the rocket state |
| `etc/deposits.flecs` | The deposits of a run, the Luminite's with their glints (`DepositSparkle`) |
| `etc/player.flecs` | The `Astronaut` template |
| `etc/props/ore.flecs` | The `Ore` template of the deposits |
| `etc/critters/` | The `Mite`, `Skitter`, `Brute` and `Behemoth` prefabs, models with their health, speed, damage, range, radius and wave tier |
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
| `etc/ui/palette.flecs` | The colors of the UI, `palette.*` |
| `etc/ui/icons.flecs` | The icon textures, `icons.*`, which the items name with `OreIcon` |
| `etc/ui/panel.flecs` | The `Panel` template, the chrome of every panel: the plate, its edge, the hazard strip and the `InputBlocker` |
| `etc/ui/hud.flecs` | The `OreHud` template: the status bar (`StatusGauge`, `StatusDivider`), the toast, the mine progress bar, the suit breach alert and the rocket status line |
| `etc/icons/` | The original's icons (SVG), unchanged |
| `etc/ui/hazard_strip.svg` | The original's hazard strip of the panels, unchanged |
| `src/flow.c` | States, clock, blink, toast, seeds, `oreelse.cancel()`, `oreelse.restart()` |
| `src/map.c` | The tile grid, the deposits' registration and the hover tile |
| `src/player.c` | Walking, the jetpack and the camera; the astronaut's animation (`OrePlayerFx`: the walk cycle `OrePlayerGait`, the bob, the foot dust and the jet plume; `OreLimbs`: the limbs) and its death (`ore_player_die`) |
| `src/inventory.c` | The inventory and the item components |
| `src/mining.c` | Mining, demolishing and their tile marker; the burst and the shrink of a mined deposit, the poofs of a used up deposit and of a demolished building, the glints of the Luminite while playing |
| `src/critters.c` | The waves, the critters' targets, attacks and moves, the crowd separation and the critter spatial index, their deaths, the astronaut's health and regen, `oreelse.spawn_critter()` |
| `src/critter_fx.c` | The critters' walk, bob, waddle and legs, their death bursts, the corpses' char, sinking and cap, `oreelse.corpse()` |
| `src/buildings.c` | The buildings in the tile grid, the wall connectors, demolish and salvage, destroyed buildings, the look of the ghosts |
| `src/building_fx.c` | The buildings' animation and lights at the work rate of their building: the spinners, pistons, shakes, vents, work emitters and work lights |
| `src/placement.c` | The selection, `canPlace`, the click and the drag, the poof of a placed building, the pipette, the preview, `oreelse.select()`, `oreelse.pipette()`, `oreelse.placeStart()`, `oreelse.placeEnd()`, `oreelse.cancelDrag()` |
| `src/craft.c` | The order book, the workers, `oreelse.craft_order()`, `oreelse.craft_cancel()` |
| `src/power.c` | Production, demand and the blackout, the drills, the factories' crafters, the status lamps, `oreelse.power_set(category, level)` |
| `src/rocket.c` | The rocket state, installing, uninstalling, loading, the launch and the flight, `oreelse.rocket_install()`, `oreelse.rocket_uninstall()`, `oreelse.rocket_load()`, `oreelse.rocket_launch()`, `oreelse.craft_key()` |
| `src/look.c` | The render quality tiers, `oreelse.cycleQuality()` |
| `src/particles.c` | The helpers that play the effects (`ore_burst`, `ore_burst_aimed`, `ore_explosion`, `ore_combust`), the flash of their lights, `oreelse.explosion()`, `oreelse.combust()` |
