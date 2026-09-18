# Renderer side evaluation

Transition state travels on the frame packet and is evaluated by the renderer thread.

## Details

Extract writes a transition record only for entities that have one, carrying the start value, the target, the start time and the parameters; the renderer keeps it next to the instance, evaluates it every frame until it ends and then drops it. A static entity with a transition therefore stays static on the game side and uploads once per assignment. Finished transitions are reported back with the returned packet so the game side clears its bookkeeping without polling.

Verification: unit tests cover the packet contents for entities with and without transitions, the renderer side values at known timestamps and the completion report. A visual test captures one transition at three timestamps, and a benchmark proves a scene without transitions pays nothing.
