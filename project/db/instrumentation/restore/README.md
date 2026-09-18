# Checkpoint restore

restore and restore [checkpoint] bring a running instance back to a saved state.

## Details

restore reloads the script files of the last checkpoint through the reload entry point, restore with a name does the same for a named one, and both report what changed. Restoring uses the same script ownership information as writing, so entities created outside a script are left alone and reported. A failed restore leaves the world untouched.

Verification: unit tests restore a checkpoint after modifying entities and check the world matches, and cover an unknown name and a corrupt checkpoint. A command based integration test captures a frame, changes the scene, restores and captures again, where the two images must match; the log and both images are the deliverables.
