# Declarative spatial layout

Import `box.spatial` alongside `box.geometry` and `box.transform`. `Fit`, `Align`,
`Array`, and `Distribute` run in `PostUpdate`, before transforms and the spatial
index are updated. No explicit update call is needed in a running world. C tools
can call `box_spatial_layout_update(world)` to resolve immediately; it returns
false if any relationship fails. Use `ecs_progress` to update downstream engine
systems, including the spatial index.

Run the complete ship and wall example:

```sh
bake3 run apps/boxrun --local-env -- --design --surface 1280x720 etc/scenes/spatial_layout.flecs
```

## Fit

```flecs
using box.geometry
using box.spatial

hull { Box: {8, 3, 18} }
armor {
    Box: {1, 0.2, 1}
    Fit: {
        target: hull,
        axes: XZ,
        padding: {0.4, 0, 0.8},
        mode: Stretch
    }
}
```

The resolved armor dimensions are `{7.2, 0.2, 16.4}`. Axes are `X`, `Y`, `Z`,
`XY`, `XZ`, `YZ`, or `XYZ`; omission selects `XYZ`. Padding is removed from both
sides in the target's geometry coordinates. `Stretch` is the default.
`Contain` chooses the smallest selected-axis ratio; `Cover` chooses the largest.
The resulting uniform ratio applies to selected axes only; unselected axes keep
their authored dimensions.

Fit compares lengths along corresponding object axes, including inherited scale.
Rotating a target does not turn its X dimension into its Z dimension. Parent
rotation under nonuniform scale is accounted for through transformed axis lengths.
Fit changes geometry size without changing the authored transform scale. Built-in
Box geometry already uses a shared unit mesh with dimension parameters; Fit writes
those resolved dimensions to that same geometry representation. Other registered
shapes use their actual mesh or procedural surface bounds and geometry instance
scale. Fit requires geometry on the fitted entity. A target may also be a group.

## Align

```flecs
cockpit {
    Box: {3, 1, 4}
    Align: {
        target: hull,
        self: {x: Center, y: Min, z: Center},
        other: {x: Center, y: Max, z: Center},
        offset: {0, 0.1, 2}
    }
}
```

Each bounding-box coordinate defaults to `Center`. `Keep`, the default rotation
mode, preserves authored orientation. `Match` matches anchor orientation. The
oriented faces are `Front` (+Z), `Back` (-Z), `Top` (+Y), `Bottom` (-Y), `Right`
(+X), and `Left` (-X). They can be supplied directly or as `{face: Front}`.
Matched faces point toward each other. For example:

```flecs
wall {
    Box: {8, 3, 0.3}
    Rotation: {0, 0.5235988, 0}
}
window {
    Box: {2, 1.5, 0.1}
    Align: {
        target: wall,
        self: Back,
        other: Front,
        rotation: Match,
        offset: {0, 0, 0.05},
        rotation_offset: {0, 0, 1.5707963}
    }
}
```

All rotations use Box's existing radians and Y-X-Z Euler convention. The example
uses a 30-degree wall rotation and a 90-degree rotation around the attachment
normal. Rotation offsets are applied in the matched target frame; in `Keep` mode
they are applied after the authored orientation. Offsets use the target anchor's
orthonormal frame: X is its tangent, Y its up direction, Z its outward normal.
Their magnitudes are world lengths, so a gap stays constant under scaling.
Nonuniform scales use the inverse transpose for surface normals.

Named frames are direct child entities with `AttachmentFrame`:

```flecs
hull {
    Box: {8, 3, 18}
    port { AttachmentFrame: {position: {4, 0, 0}, rotation: {0, 1.5707963, 0}} }
}
dock {
    Box: {1, 1, 0.5}
    Align: {target: hull, self: Back, other: {frame: hull.port}, rotation: Match}
}
```

Frame positions are in their owner's geometry coordinates. `self.frame` must
belong to the aligned entity; `other.frame` must belong to the target. Frame
entities do not contribute geometry bounds. Named frames can attach entities
without geometry. Bounding anchors require geometry bounds.

## Array

```flecs
vents {
    Array: {axis: Z, count: 8, spacing: 0.7}
    rib { Box: {0.15, 0.3, 0.4} }
}
```

Each direct child is a prototype. Array temporarily makes authored prototypes
and their descendants prefabs and creates `count` instances of each. Their
geometry uses the existing shared mesh cache and rendering batches. Index zero
starts at the prototype's authored position. Spacing measures center-to-center
translation along the container's axis. Counts from zero through 100000 are
accepted; zero hides the prototypes and creates no instances.

Optional `rotation_step` adds `index * rotation_step` after the authored rotation.
Optional `scale_step` multiplies authored scale by `1 + index * scale_step` on
each axis; every resulting factor must be positive. Both default to zero.

`ArrayInstance` identifies the owner, prototype, and index. Surviving indices keep
their entity IDs as spacing, dimensions, and count change. Shrinking deletes only
excess instances; growing again creates new entities for those deleted indices.
Prototype geometry and transforms are synchronized, including nested children.
Removing Array deletes generated instances and restores prototypes that Array
made into prefabs. Explicitly authored prefabs remain prefabs. Generated entity
names beginning with `__array_` and `__prototype_` are reserved for layout.

## Distribute

```flecs
sensors {
    Distribute: {axis: X, span: 6, mode: SpaceBetween}
    left { Box: {0.5, 0.5, 0.5} }
    center { Box: {0.5, 0.5, 0.5} }
    right { Box: {0.5, 0.5, 0.5} }
}
```

Distribute positions distinct, enabled, non-prefab children in ascending entity
ID order. The span is centered at the container origin. Child dimensions include
Fit, authored rotation and scale, and aggregate group bounds. Other coordinates
retain their authored values. Attachment-frame children are excluded.

| Mode | Placement |
| --- | --- |
| `SpaceBetween` (default) | First and last outer edges touch the span boundaries. |
| `SpaceAround` | End margins are half the gap between children. |
| `SpaceEvenly` | End margins and interior gaps are equal. |
| `FixedGap` | Uses `gap` and centers the complete row. A zero span means unconstrained. |

One child is centered; an empty container is valid. A span smaller than the
required occupied length is an error. Array and Distribute require a single
axis (`X`, `Y`, or `Z`). A child cannot have two placement drivers: for example,
Align cannot override a position assigned by Distribute or Array. Fit can be
combined with any placement operation. A whole Array or Distribute group can
itself be aligned.

## Evaluation and authored data

Authored `Position`, `Rotation`, `Scale`, and geometry components remain intact.
Referenced geometry without an explicit `Position` receives its implicit zero
position so the engine also renders it in its parent's frame.
Computed poses use `WorldTransform` with an owned `ManualTransform` tag, and Fit
writes resolved geometry dimensions to the existing mesh representation.
`ResolvedBox` exposes a Box's computed dimensions. `IntrinsicBounds` and
`ResolvedBounds` expose geometry-local bounds before and after layout. An array's
intrinsic bounds describe its authored prototypes, while its resolved bounds
include the generated copies. Removing a rule restores the authored result.
Applications should edit authored components, rather than computed components.
An existing application-owned `ManualTransform` or `SurfaceAttachment` cannot
also be driven by spatial placement.

Evaluation follows a directed dependency graph with separate geometry-bounds,
orientation, pose, and distribution steps. Each step is evaluated once per pass. Results are
kept between frames: a frame only solves and publishes again the entities whose inputs
changed (layout rules, transforms, geometry, parents, prefabs) and the entities that depend
on them, and only arrays that contain a change sync their instances. Entities that follow a
`DynamicTransform` are solved every frame, so direct dynamic-transform writes are picked up.
A frame in which nothing changed does no layout work. Unchanged results do not emit change
events. Scenes without spatial rules skip array processing.

An entity with geometry uses its own shape as its reference bounds, excluding
children. An entity without geometry aggregates its children. This prevents
attached details from enlarging the reference hull. Authored bounds exclude
layout-generated instances. A group whose child fits to that same group's
resolved bounds is a cycle and is rejected, rather than feeding back growth.
No general constraint solving or iterative convergence is used.

`SpatialDiagnostic` records an error code and related entity. Errors are logged
when they change, and dependent rules receive diagnostics too. Codes cover dead
or missing targets, missing or zero-sized bounds, invalid attachment frames,
cycles, conflicting drivers, and impossible sizes or spacing. Failed entities
restore authored output, and diagnostics clear after the cause is corrected.
Independent layouts continue to resolve.

The C component corresponding to script `box.spatial.Align` is `SpatialAlign`,
which avoids a collision with the existing UI `Align` enum. Existing scripts
that do not use spatial rules keep their existing behavior.
