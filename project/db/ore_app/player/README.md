# Player and camera

The player walking and running with reach, mining and demolishing on hold, and the following camera.

## Details

Port player.c and player.flecs: movement from the game's actions through the box input maps, the
camera following with the game's offset and zoom by scroll unless the UI holds the mouse, reach
checks against cells, mining a deposit and demolishing a building on right mouse hold with progress,
and the pistol as the first weapon (firing is the combat epic).

Verification: Unit tests for movement, reach and mining progress on a fixed clock, a command based test walking to
a deposit and mining it, a visual scene.

## References

- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/src/player.c
- /Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/player.flecs
- project/db/controllers/deliverable/README.md
