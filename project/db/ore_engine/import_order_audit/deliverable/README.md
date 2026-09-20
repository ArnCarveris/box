# Consistent module import order

## Found
A `test/core` Modules case that imported every box module alone in its own world, then all of
them, failed on the ids the flecs define macros force: a component id cached from the first
world lands on whatever entity that number is in a world that registered things in another
order (`Shader` on `box.core.App`), and modules that guarded a lazy path on a cached id of a
module they do not import created observers on ids that were dead or someone else's.

## Built
The flecs-engine way, at the request of the owner: a `Box` module (`src/box/box.c`,
`BoxImport` in `include/box.h`) imports `FlecsScriptMath` and every box module in one fixed
order, and an application, a test or a benchmark imports `Box` and nothing else, so every
world registers the same entities in the same sequence and the cached ids hold in all of
them. `box_app`, every test project and every bench project were switched; a few test files
whose subject is a minimal world (the input maps alone, the job pool alone, a world without
the particle module, the transition allocation count, the script loader itself) keep their
subset import, which is safe because every test case runs in its own process. The five lazy
guards (collision shapes and debug, effects, the fps overlay, the console) compare the cached
id with the entity of that name in this world. The Modules case now checks that every module
lives under `box`, that two worlds get the same ids and that a second import adds nothing.

Importing everything everywhere surfaced three engine bugs, fixed here: `box.collision`
registered template instances as roots only through the pair the incremental loader adds and
now also uses `(flecs.script.ScriptTemplateRoot, T)`, which flecs puts on every instance; a
root whose AABB covers nothing takes the bounds of its collider, so a picking target that is
a template root is found; the fps overlay of a world without View entities reads the
singleton again. The command module's log capture asserted when a second live world imported the module and
deadlocked on its own lock while reporting it; the first world captures, later ones keep an
empty buffer and the lock lives until the last one is gone. Incremental template loading became an application setting
(`ScriptSettings.enabled`, off by default, `box_app` turns it on), because every world now
has `box.script` and a test expects an instance the moment it is declared; enabling it takes
over the templates that already exist.

## Verified
`test/core` Modules 3 cases; every test project green (checkpoint 41, collision 89, command
222, controller 58, core 49, effects 7, geometry 115, gltf 32, input 98, job 20, lod 151,
monitor 67, particle 175, picking 55, render 415, script 23, shader 30, spline 67, transform
83, transition 40, ui 246, view 147); every visual and command scene passes. `picking_hover`
was refreshed: with the input update before the pick in every world, the cursor sweep of the
scene hovers the box it crosses on its way, which the old order missed by a frame.
`console_ls.expected` refreshed for the `scenes` scope. `bench: false`.
