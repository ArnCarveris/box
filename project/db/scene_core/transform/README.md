# Transform module

Position, Rotation, Scale, Orientation, LookAt and WorldTransform with observer driven static updates and per frame dynamic updates.

## Details

`box.transform` keeps `WorldTransform = parent * T * R * S`. Static entities, meaning no `DynamicTransform` on themselves or an ancestor, are recomputed by observers when a pose component is set, when they are reparented or when they lose the dynamic tag. Each recompute walks the subtree once, parents before children, and writes with `ecs_set` so the render module receives one OnSet per entity.

Entities under `DynamicTransform` are recomputed every frame in PostUpdate by a cascade ordered system that writes in place. `LookAt` writes `Rotation` so local +Z faces the target and `Orientation` wins over `Rotation`. `ManualTransform` entities are never written by the engine. 46 tests cover composition, three level hierarchies, reparenting, dynamic parents with static children and a 100 deep chain.
