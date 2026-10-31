# Space station

A small management game written only in flecs script. You run a station in orbit: extend it with new modules, keep the residents alive, earn money from visitors and trade, and keep enough docks free for the ships that call.

Run from the repository root:

```sh
bake3 --local-env
bake3 run apps/boxrun --local-env -- etc/scenes/space_station.flecs
```

## Goal

Start as a Waystation with 8 residents and 2500 credits. Reach 20 residents to become an Outpost, 50 for a Colony, and 100 residents with 20000 credits for a Starport. You lose when everyone has left or when your debt passes 1500 credits.

## Controls

| Input | Action |
| --- | --- |
| Left click on a Construction tile, or `1` to `9` | Pick a module, again to put it away |
| Left click on a green bay | Build the module, which must connect to the station, and put the tool away |
| `X`, then left click on a module | Sell it for half its cost and put the tool away |
| Left click anywhere outside the Construction panel, or `Escape` | Put the tool away |
| `P`, or the speed buttons | Pause, or run at 1x, 2x or 4x |
| Hover a dial or meter in the top bar | Show its exact values at the bottom of the screen |
| Hold right mouse, `W` `A` `S` `D` `Q` `E` | Move the camera |

The other cameras are `space_station.dock_camera` and `space_station.keel_camera`.

## Modules

| Module | Cost | Power | Effect |
| --- | --- | --- | --- |
| Solar Array | 350 | +7 | |
| Fusion Reactor | 1600 | +32 | High upkeep |
| Life Support | 550 | -3 | Supports 16 people |
| Atmosphere Plant | 450 | -3 | Makes 3 air per second |
| Hydroponics | 400 | -2 | Grows 0.8 food per second |
| Habitat Ring | 500 | -1 | Houses 12 residents |
| Passenger Dock | 650 | -2 | Lands one visitor ship at a time |
| Cargo Dock | 650 | -2 | Lands one freighter at a time |
| Cargo Bay | 250 | -1 | Stores 80 more tonnes |

## How it plays

Everyone on board breathes air and eats food, visitors included. A power shortage slows air and food production and weakens life support. Residents move in while there is housing, food, air and life support to spare, and pay rent. They leave when something runs out.

Visitor ships pay to land and their passengers spend money while they are docked. A docked freighter opens the cargo market: buy food when your farms fall short, and buy goods from one ship to sell at a better price to another. Prices change with every freighter.

Ships that find no free dock wait above the station. If they wait too long they leave and your reputation falls, and reputation sets how often ships come.

## Files

`simulation.flecs` holds the frame loop and the economy, `traffic.flecs` the ships, `commands.flecs` the player actions and `state.flecs` the game state and module tables. `hud.flecs` is the interface, `slot.flecs` a build bay and `ship.flecs` a ship. Every module, ship and the deck has its own asset file, with the materials in `materials.flecs` and the shared parts (pipes, vents, hatches, antennas) in `detail.flecs`.

`assets/generate.py` writes the textures, the environment and the icons in `assets`. It needs numpy and Pillow.

`space_station.flecs` includes the asset files in dependency order. The asset files include nothing themselves, and a template is defined above the templates that use it: a template used before its definition keeps the script from loading.

The engine has no timer that a script can await, so `simulation.flecs` makes one: two async blocks that signal each other through `ui.changed` and `on.change` wake once per frame.

The async blocks of a template restart when anything the template reads changes, other than its own `mut` variables. An event that arrives during a restart is lost. So the game logic runs in async blocks at the root of a script, which do not restart, and slot and ship state is packed into `u64` bit fields, because a script can only assign whole variables. Templates only draw. The few that take input, such as `StationSlot` and `StationButton`, read no game state and only write requests.
