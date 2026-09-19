# Save and restore

Saving and restoring a game through the box checkpoint commands.

## Details

Mark the game's scripts as checkpointable, keep runtime state in components the checkpoint writes,
and make restore rebuild derived state (grid, order book, waves); expose save and load actions.

Verification: A command based test that saves mid game, changes the world and restores it.

## References

- project/db/checkpoints/deliverable/README.md
