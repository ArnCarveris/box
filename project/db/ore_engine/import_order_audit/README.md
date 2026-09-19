# Consistent module import order

Component ids are forced from process globals into every new world, so two worlds that reach a module through different import orders collide; audit every box module, test and bench import chain for one canonical order or make registration order independent.

## Details

`component_id_drift` fixed one pair (`box.particle` reached `box.transform` before `box.shader`,
`box.geometry` after it) by swapping two imports, but every module that imports its dependencies
in an order that differs from the chain `BoxRender` establishes has the same collision waiting,
and so does every test or bench project that imports modules directly. Either derive one
canonical order (each module imports its dependencies in the order the dependency graph sorts
them, checked by a test that imports every module alone in a fresh world and then all of them,
comparing the ids each global holds) or stop forcing ids: register components through a helper
that looks the entity up by name in the new world before forcing the cached id. Prefer the
second if flecs allows it, since it removes the class of bug; state in the deliverable why the
other was chosen if not.

Verification: a `test/core` case that creates one world per box module in sequence and then one
with all of them and finishes cleanly, every test project green, every bench project runs.

## References

- `project/db/ore_engine/component_id_drift/deliverable/README.md`, `src/particle/particle.c`,
  `src/render/render.c`, `test/checkpoint`, `bench/*/src`
