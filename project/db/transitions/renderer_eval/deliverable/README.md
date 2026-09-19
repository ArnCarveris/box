# Renderer side evaluation

A transition of a static instance travels to the renderer once. Extract writes a
`box_transition_cmd_t` (mesh, slot, assigned values, parent transform, mesh scale and bounds)
and one `box_transition_rec_t` per instance channel it moves; the renderer thread recomposes
the transform, bounds, color and emission of every later frame in `box_transition_fill`,
rewrites the slot and appends the instance to the static updates of the packet, so the shadow
cache and the bake descriptors see it. A finished transition keeps its final value, is retired
and reported in `transitions_done` of the returned packet, which the next extract turns back
into the entity. The interpolation is the shared inline `box_transition_lerp`, still used by
the game side for dynamic and sorted instances.

Verified by 572 unit tests (35 in `test/transition`, the new `Renderer` suite covering the
packet with and without a transition, the renderer values at known timestamps, the final value,
the completion report, a `Position.x` member transition in script and the game side fallback),
all 73 visual scenes bit identical including `transitions.png`, `bench_transitions_10k.json`
(10k entities, 10 transitions each, one restart per entity per second, 10000 static and 0
dynamic instances, 9.2 ms mean, no spikes) and `bench.json`: extracting that scene fell from
1.9 ms to 79 us per frame against 34 us without transitions, while `fill_10k_instances` puts
the renderer side at 375 us for 10k instances. Left out: a Transition that splits one channel
over two parameter sets keeps that entity on the game side, which re-extracts it every frame.
