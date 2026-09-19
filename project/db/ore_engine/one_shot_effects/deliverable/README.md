# One shot effects at a world point

## Built

An effect is an ordinary template: a prefab holding the emitter, its bursts, its curves,
its appearance and an optional `PointLight` with an `EffectFlash`. `box_particle_burst_at
(world, effect, position, scale)` and the script function `particle.burst_at(effect,
position, scale)` take a free instance of that template from its pool, or make one when the
pool is empty, place it, scale its sizes, speeds, spawn volume and acceleration and play it
from the top. The engine ends the emission once the bursts of the template have gone off
and hands the instance back to the pool a lifetime later, which is the frame its last
particle has died, so a hundred hits a second cost the entities of one pool instead of one
entity per hit. The flash falls from the intensity of the template to nothing over its
seconds and lights again on the next play; it holds the instance when it outlasts the
particles. `ParticleEffect` on the instance names the template, the scale and the times the
emission ends and the instance is freed at.

## Verified

17 new tests in `particle.Effect`, 174 in `test/particle`, all green; lint 0. The new
`one_shot_effects` visual scene plays one template at scale 0.6, 1 and 1.8 a frame into the
burst with the flashes on the ground and renders bit identical on repeat runs; all 93 other
scenes match and all 16 command scenes pass. A play costs 612 ns and a frame of 100 playing
effects 0.78 us; every earlier `bench/particle` case is within a few percent of the
particles epic.

## Left out

An effect template is one entity, so a template whose light or decal is a child of its
emitter is not covered; the pool keeps the seed of the template, so a replay draws the same
particles; and the play has no component form, only the C call and the script function.

## Note

A scene declares an effect template with `prefab Spark { ... }`. The `Spark : Prefab { }`
form of `hidden.flecs` gives the entity `(IsA, Prefab)` instead of the prefab tag, and its
emitter then plays at the origin.
