# Colliders

## Built

`Collider`, written by the engine in world space with the instance scale
applied, derived from the shape where a cheap test exists: `Sphere` and
`IcoSphere` give a ball, `Box` and `RoundedBox` an oriented box, `Capsule` a
capsule and every other shape its world bounds. `SphereCollider`, `BoxCollider`
and `CapsuleCollider` override the derived one and `NoCollider` opts out. The
ray, sphere and capsule tests and the bounds of a collider are public functions
in `box/collision.h`. The module stays independent of `box.geometry`: it binds
the shape observers when that module is present.

## Verified

`test/collision` suites `Colliders` and `Intersect`, 40 cases: derivation per
shape, overrides, opt out, scale, rotation, updates on move and shape change,
and every test against every collider kind. `bench/collision` suite `Colliders`:
1000 of 100k colliders resolve in 218 us, a ray test costs 2.7 ns.

## Left out

The debug scene that draws colliders as wireframes needs `test/integration` and
the renderer, which this agent does not own; it is left as follow up work.
