# Transition components

`box.transition` with `Transition` (a map component from a component or member
entity to `TransitionParams`), `TimingFunction`, `TransitionValue` and the
`Transitionable` registration point for any reflected component of up to four
floats or bytes. Assigning a covered component starts a transition from the
value currently displayed, kept in a private `TransitionState` that only
entities with a transition carry. `box.render` reads the displayed value in
Extract, recomposing the pose under the transform of the parent, and re-reads
such an entity every frame until a view has shown the end of it. The
interpolation is `box_transition_eval` over one record and touches nothing else,
which is what `renderer_eval` moves onto the renderer thread.

Verified by 27 unit tests (`test/transition`: starting, replacing, delay,
completion, every timing function, prefab inheritance, a value assigned twice in
a frame, an unsupported component, the published pair, and the instance data of
color, position, a transitioning child, emission and tint), 370 render tests,
the visual scene `transitions` (captured halfway, `transitions.png`) with the
other 72 scenes still matching, and `bench/transition`: extracting 10k entities
with 10 transitions each is 1.9 ms, the same scene without transitions 34 us and
100k records evaluate in 166 us.

Left out: member level keys cannot be written in a script, because this flecs
only creates member entities under `FLECS_CREATE_MEMBER_ENTITIES` and its
expression parser reads `Position.x` as member access; the code resolves them
when member entities exist. `Opacity` transitions are recorded but an entity
crossing into transparency moves to the sorted pass.
