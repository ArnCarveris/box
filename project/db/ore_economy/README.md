# Ore Else economy

Resources and items, the inventory, the crafting order book that queues prerequisites itself, repair drones and the rocket assembly that ships Luminite and wins the game.

## Details

The economy is the heart of the game: ore and item definitions, an inventory with capacity, a
crafting system where ordering an end item queues its part tree and prices on order and stalls on a
full inventory, repair drones flying from pads to damaged buildings and back with iron, and rockets
assembled step by step on a pad, loaded with Luminite and launched, with the life support module as
the win condition.

Verification: Unit tests over the order book, the allocator interplay and the rocket state machine on a fixed
clock, command based tests that craft, repair and launch, and the queue and rocket panel scenes once
the UI epic lands.

## References

- design/DESIGN.md
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/README.md
- project/db/ore_engine/README.md

## Porting rule

Ore Else was built on an older, inferior engine, and this port is the showcase for how box does it
better. Do not carry the old engine's structure, workarounds or C where box has a declarative way:
prefer script templates, components and the command interface over C systems, keep gameplay logic
in C only where it is genuinely simulation (grids, economy, AI), and where the old game worked around
a missing feature, file the feature as a box task instead of porting the workaround. Every screen,
effect and building should read as an example of the box way.
