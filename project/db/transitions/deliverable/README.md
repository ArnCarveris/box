# Transitions: completion report

## What was built
- `box.transition`: `Transition` is a map component from a component or member entity to `TransitionParams {duration, delay, timing}`, inherited from prefabs, and `Transitionable` registers any reflected component of up to four floats or bytes. The author facing component always keeps the assigned value; assigning it starts a transition from the value currently displayed, recorded in a private `TransitionState` that only transitioning entities carry, so everything else costs nothing.
- A transition of a static instance travels to the renderer once: extract writes a command with the assigned values, parent transform, mesh scale and bounds plus one record per instance channel, and the renderer thread recomposes transform, bounds, color and emission each frame, rewrites the slot and feeds the static updates so shadows and probes follow. A finished transition keeps its final value and is reported back so the entity is retired from the game side.
- Member keys such as `Position.x` work in scripts now that flecs builds with `FLECS_CREATE_MEMBER_ENTITIES`.
- At scale the records live in per channel lanes (`times`, `spans`, `outs`) in a pool that never regrows, evaluated by one branch free four float pass per channel; `transitions_10k.flecs` staggers ten transitions per entity over the second so 60k records stay in flight.

## Verification
- TESTS_PLACEHOLDER
- Scenes in `integration/`: `transitions` captured halfway through color and position transitions under the fixed timestep; `transitions_10k.json` records 10k entities with ten transitions each restarted every second; every earlier scene bit identical.
- Benchmarks in `bench/` against the UI epic: extracting the 10k by 10 scene costs 79 µs a frame against 34 µs without transitions, down from 1.9 ms when the game side evaluated every frame; the renderer side fill of 10k instances costs 375 µs. The staggered 10k scene runs at 9.1 ms a frame (110 fps, no spikes) with 808 bytes of packet per starting entity and 135 KB steady, the lane pass over 60k records costs 115 µs.
- Build reports and the wasm image in the deliverable, open it through the status page Inspect link of this epic.

## Lessons
- Writing the record once and evaluating on the renderer thread is what makes the main thread cost independent of the number of active transitions.
- A component with reflected members only gets member entities under a flecs build define; without it a script cannot name `Position.x`.
- A transition that splits one channel over two parameter sets stays on the game side and is re-extracted every frame.

## Not in this epic
- `Opacity` transitions are recorded, but an entity crossing into transparency moves to the sorted pass mid transition.
- Transitions of dynamic and sorted instances are still evaluated on the game side.
