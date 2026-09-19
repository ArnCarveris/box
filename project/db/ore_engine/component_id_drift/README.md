# Component ids drift across worlds

A bench case that imports BoxGeometry after worlds that imported only BoxParticle crashes on component id drift; module import order must not change component ids within a process.

## Details

The rain task found that creating a world importing only `box.particle`, finishing it, and then creating one that imports `box.geometry` crashes. Box registers components through the flecs C module API with global ids, so a component registered second in one world and first in the next gets a different id than its global variable holds unless every module registers with the same ids. Write the reproducer as a `test/particle` or `test/core` case running the two worlds in sequence, find the registration that skips `ecs_component_init` with the cached id (or the module that registers a component of another module by value), fix it in `src`, and check every module pair the bench projects use.

Verification: the reproducer fails before the fix and passes after, every test project green.

## References

- `project/db/particles/rain/deliverable/README.md`, `src/particle/particle.c`, `src/geometry/geometry.c`
