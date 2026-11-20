# Movement

Constant-rate motion: integrates `Velocity` into `Position` and `AngularVelocity` into
`Rotation` every frame, and moves `PathFollow` entities along a `Path3D` at a constant speed.
Module `box.movement` (`BoxMovementImport`, imports `box.transform` and `box.geometry`).

## Components

| Component | Fields | What it does |
|---|---|---|
| `Velocity` | `x, y, z` (units/s) | `Position += Velocity * dt` |
| `AngularVelocity` | `x, y, z` (rad/s) | `Rotation += AngularVelocity * dt` (Euler angles, per axis) |
| `PathFollow` | `path` (entity with `Path3D`), `speed` (units/s, may be < 0), `distance`, `mode`, `orient` | `distance += speed * dt` by arc length, `Position` = path point at `distance` |
| `Pan` | `center`, `amplitude` (rad), `speed` (rad/s), `phase` (rad) | `Rotation.y = center + amplitude * sin(world time * speed + phase)`, x and z kept (a panning security camera) |

`PathFollowMode`: `PathFollowOnce` (clamp at both ends), `PathFollowLoop` (wrap the distance),
`PathFollowPingPong` (reflect at the ends and flip the sign of `speed`).

All are in the parent's local space: they change the local `Position`/`Rotation`, children
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

// A patrol route (needs `using box.geometry`), points in the follower's parent space
route { Path3D: {start: {0, 0, 0}, segments: [{kind: CatmullRom, end: {10, 0, 0}},
  {kind: CatmullRom, end: {10, 0, 10}}, {kind: CatmullRom, end: {0, 0, 10}}], closed: true} }
patrol { PathFollow: {path: route, speed: 5, mode: PathFollowLoop, orient: true} }
```

## Systems

| System | Phase | Query |
|---|---|---|
| `box.movement.IntegrateVelocity` | `EcsOnUpdate` | `Position` (inout), `Velocity` (in) |
| `box.movement.IntegrateAngularVelocity` | `EcsOnUpdate` | `Rotation` (inout), `AngularVelocity` (in) |
| `box.movement.FollowPath` | `EcsOnUpdate` | `Position` (out), `PathFollow` (inout), `?Rotation` (out) |
| `box.movement.PanYaw` | `EcsOnUpdate` | `Rotation` (inout), `Pan` (in) |
| `box.movement.PathFollowInvalidate` | observer `OnSet`/`OnRemove` | `Path3D` (self, also on prefabs) |

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
- `PathFollow` implies `Position` and `DynamicTransform`; with `orient` the system adds
  `Rotation` the first frame. Orientation looks down -Z along the tangent of travel with +Y up
  (+X up when the tangent is along Y), the frame `Sweep` uses; roll is always 0.
- Motion is by arc length: an arc length table (64 samples per curved segment, 1 per line) is
  built once per entity that owns the `Path3D` and dropped when its `Path3D` is set or removed.
  Changing a path in place through `ecs_ensure` without `ecs_modified` keeps the stale table.
- `path` may inherit `Path3D` from a prefab, or be the prefab itself; the table belongs to the
  owner. A closed path includes its closing segment; `PathFollowLoop` on an open path jumps
  from the end back to the start.
- `distance` is written every frame and can be set to seek. `PathFollowPingPong` writes
  `speed` (its sign flips at each end).
- A missing or invalid path (no `Path3D`, no segments, non-finite points) leaves the entity
  where it is and logs one error per follower and path.
