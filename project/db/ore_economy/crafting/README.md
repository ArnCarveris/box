# Crafting order book

Ordering an end item queues its part tree, prices on order and stalls on a full inventory.

## Details

Port craft.c: recipes, the order book with part trees, priority and cancellation, factories pulling
orders they can make, the priced on order semantics and the stall on a full inventory, exposed as
components the queue UI reads.

Verification: Unit tests over the order book on a fixed clock including cancellation and stalls, a command based
test ordering a turret from raw ore.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/craft.c
