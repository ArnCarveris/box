# Colliders

Colliders derived from the shape where possible, with explicit overrides and an opt out.

## Details

A primitive gets a cheap collider for free: a sphere gets a sphere collider, a box an oriented box, a capsule a capsule and anything else its AABB or a convex hull. An explicit collider component overrides the derived one and NoCollider opts out entirely. Colliders are stored in one place with the instance scale applied, because picking and controller collision both query them.

Verification: unit tests cover derivation per shape, override, opt out, scaling, and the ray, sphere and capsule tests against every collider type. A debug visual scene draws colliders as wireframes over the primitives scene.
