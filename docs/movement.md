# Movement

Constant-rate motion: integrates `Velocity` into `Position` and `AngularVelocity` into
`Rotation` every frame. Module `box.movement` (`BoxMovementImport`, imports `box.transform`).

## Components

| Component | Fields | What it does |
|---|---|---|
| `Velocity` | `x, y, z` (units/s) | `Position += Velocity * dt` |
| `AngularVelocity` | `x, y, z` (rad/s) | `Rotation += AngularVelocity * dt` (Euler angles, per axis) |

Both are in the parent's local space: they change the local `Position`/`Rotation`, children
follow through the transform hierarchy.

## Flecs script

```flecs
using box.transform
using box.movement

// A camera rig that orbits slowly: rotate the parent, the child camera follows
orbit {
  Position: {80, 0, -60}
  AngularVelocity: {0, 0.015, 0}
  arm {
    Rotation: {-0.4, 0, 0}
    eye { Position: {0, 0, 1700}; Camera: {fov: 40} }
  }
}

drone { Velocity: {0, 0, -5} }   // gets Position {0,0,0} if it had none
```

## Systems

| System | Phase | Query |
|---|---|---|
| `box.movement.IntegrateVelocity` | `EcsOnUpdate` | `Position` (inout), `Velocity` (in) |
| `box.movement.IntegrateAngularVelocity` | `EcsOnUpdate` | `Rotation` (inout), `AngularVelocity` (in) |

Runs before the transform systems in `EcsPreStore`, so world transforms are current the same frame.

## Rules and gotchas

- `Velocity` implies (`With`) `Position` and `DynamicTransform`; `AngularVelocity` implies
  `Rotation` and `DynamicTransform`. A missing `Position`/`Rotation` starts at zero.
- `DynamicTransform` stays on the entity after the velocity is removed (it moves the entity into
  the dynamic part of the transform/spatial systems; see `box/transform.h`).
- Integration is plain Euler with `it->delta_time`: no damping, no acceleration, no collision.
  Use `box.controllers` for collision-aware input-driven motion.
- Positions/rotations are written in place by the systems: no `OnSet` is emitted for them.
- Angular velocity adds to Euler angles component-wise; it is not a true angular velocity about
  a world axis when more than one component is non-zero.
