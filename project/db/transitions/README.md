# Transitions

Transitions for authorable continuous values, evaluated on the renderer thread so only entities that have them pay for them.

## Details

A Transition component maps a component, or a member of one, to a duration, a delay and a timing function. The author facing component always holds the value the author assigned, and assigning a new value starts a transition from the currently displayed value, so nothing in the world model becomes approximate. The transition state travels on the frame packet and the renderer thread evaluates it, which means a transitioning static entity does not become dynamic and uploads only when its target changes.

Verification is unit tests for starting, replacing, delaying and finishing transitions and for each timing function, renderer side evaluation checked at known timestamps, visual captures of the same transition at three times, and the brief's benchmark of 10k entities with ten transitions each. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Transition, TransitionParams, TimingFunction and TransitionValue from the transition datamodel.

## References

- design/DESIGN.md, section Transitions
- design/datamodel/transition.flecs
