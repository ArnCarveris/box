# Incremental template loading

Template instantiation spread over frames, nearest to the camera first, with non template content loading immediately.

## Details

Use the incremental instantiation support in flecs to instantiate templates a bounded number of entities per frame, driven by a budget in JobSettings, while non template entities load immediately so the ground and the structure of a scene appear first. Pending instantiations are ordered by the distance from their position to the active camera and re-ordered when the camera moves, so the scene fills in around the player. Script parsing itself stays synchronous, which the brief allows.

Verification: unit tests cover the per frame budget, ordering by distance, re-ordering on camera movement, and that an instantiated entity is complete when it appears. A benchmark loads a 100k entity template scene and reports the per frame cost and the worst frame time, which must stay inside the budget.
