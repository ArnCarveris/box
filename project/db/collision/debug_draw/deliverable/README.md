# Collider debug drawing

## Built

`CollisionSettings.debug` (`DebugNone`, `DebugColliders`, `DebugBounds`,
`DebugAll`) makes the extract of a frame turn the colliders the frustum of the
first view reaches and the root bounds it covers into line segments: a sphere
as three circles, a box as twelve edges, a capsule as the circles of its two
caps with four lines between them, the bounds fallback as twelve edges, and a
root as a box in white. The segments travel in the packet as a list of colored
vertices, which the renderer draws with a line list pipeline after the opaque
and the sorted pass, depth tested and without writing depth. That list is a
generic facility (`box_lines_upload`, `box_lines_record`, `etc/shaders/lines.wgsl`)
a later epic reuses by pushing into `packet.lines`; a packet without lines
compiles no pipeline, allocates no buffer and records no pass.

## Verified

`test/collision` suite `Debug`, 8 cases: the line count per collider kind, root
bounds, both at once, a collider outside the view, the reset between packets and
the empty list while the setting is off; 88 collision and 369 render cases pass,
lint 0. `collision_debug.flecs` is the baselined scene and the other 63 visual
baselines still match bit for bit. `bench/collision` suite `Debug`: the extract
of a frame with the drawing off costs 4 ns, 10k colliders in view cost 0.80 ms.

## Left out

The wireframe of a shape without a cheap collider is missing because the module
never derives one from the bounds the renderer writes: `box_render_update_aabb`
uses `ecs_get_mut` and `ecs_ensure` without a modified, so the `AABB` observer
of `collision/colliders` never fires and the entity gets no collider at all. The
cone of the scene shows that gap; an `AABB` a script sets does produce the
fallback collider and draws it. Lines are drawn in every view and are tone
mapped with the scene when the view runs an effect chain.
