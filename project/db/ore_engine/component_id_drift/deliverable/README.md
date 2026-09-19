# Component ids drift across worlds

`BoxParticleImport` imported `BoxTransform` before `BoxGeometry`, while
`BoxGeometry` pulls in `BoxTransform` transitively through `BoxRender`,
which imports `BoxSurface` (and so `BoxShader`) before `BoxTransform`. A
world that only imports `box.particle` therefore registers `box.transform`
before any shader module runs; a later world that only imports
`box.geometry` reaches `box.transform` after `box.shader`, so
`DynamicTransform`'s cached global id (forced onto the new world) collides
with the debug singleton child flecs creates for the `box.shader` module,
aborting the process. Fixed by importing `BoxGeometry` before `BoxTransform`
in `src/particle/particle.c`, matching the order `BoxRender` already
establishes.

Verified: a new `Emitter` reproducer in `test/particle` (particle-only
world, `ecs_fini`, then a geometry-only world) crashes before the fix and
passes after; the full `test/particle` suite (175 cases) and `test/core`
(43 cases) are green, `bench/particle` (15 cases) unaffected, lint 0.

Left out: a general audit of every module pair across all ~40 box modules;
`test/checkpoint`, `test/render` and `bench/checkpoint`/`bench/command`
combine `BoxTransform` with other modules directly too and are owned by
other agents, so this task only fixes the particle/geometry pair the rain
bench hit and flags the wider ordering risk as follow up.
