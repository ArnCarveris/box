#ifndef FLECS_ENGINE_SPLINE_H
#define FLECS_ENGINE_SPLINE_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SPLINE_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Spline: paths, and geometry that follows them.
 *
 * A road, a rail, a cable, a pipe run or a patrol route is a curve first and
 * geometry second. This module keeps the curve as data - a list of control
 * points on an entity - and then answers the three questions everything else
 * asks of it: how long is it, where is metre 240 of it, and what does a
 * cross-section swept along it look like.
 *
 * Setting a Spline builds an arc-length table for it, so every query is in
 * metres along the curve rather than in the curve's own parameter, and a
 * lamp post placed at s = 200 sits 200 m from the start no matter how the
 * control points bunch up. Sampling is a binary search over that table.
 *
 *
 * Spline - the path
 *
 *   highway {
 *     Spline: {
 *       points: [{-120, 10, -80}, {-40, 10, -30},
 *                {40, 10, 30}, {120, 10, 80}],
 *       kind: CatmullRom
 *     }
 *   }
 *
 * Points are written {x, y, z} inside the list, the way mesh_ops profiles
 * are: script reads a nested [ ] as a collection, and a point is a struct.
 *
 * kind picks how the points are joined:
 *   CatmullRom  a smooth curve that passes through every control point.
 *               tension (default 0) flattens it: 1 is a straight polyline.
 *   Bezier      cubic segments: point, handle, handle, point, handle, ... so
 *               a curve of n segments needs 3n + 1 points.
 *   Linear      straight segments between the points.
 *
 * closed joins the last point back to the first. The frame carried along the
 * curve is rotation-minimizing (it does not spin around the tangent), which
 * on a closed loop can leave a small seam of twist where the ends meet.
 *
 *
 * Sweep - push a cross-section along a spline
 *
 * profile is a closed polygon in the (right, up) plane of the curve, wound
 * counter-clockwise as seen looking along the direction of travel; the loop
 * closes from the last point back to the first, so do not repeat it. It may
 * be concave, which is what makes a road deck with barriers one profile
 * instead of five boxes.
 *
 *   deck {
 *     Position3: {}
 *     Sweep: {
 *       spline: highway,
 *       profile: [{-6, 0}, {6, 0}, {6, 1}, {5.5, 1}, {5.5, 1.8},
 *                 {5.2, 1.8}, {5.2, 1}, {-5.2, 1}, {-5.2, 1.8},
 *                 {-5.5, 1.8}, {-5.5, 1}, {-6, 1}],
 *       step: 2,
 *       cap_ends: true,
 *       uv_scale: 4
 *     }
 *     Rgba: {150, 150, 155}
 *   }
 *
 * step is the distance in metres between rings (default 2): the smaller it
 * is, the more faithfully the sweep follows a tight curve. s0 and s1 cut a
 * range out of the spline; leaving both at 0 sweeps the whole thing, which is
 * how an on-ramp reuses its parent's spline over a stretch of it. cap_ends
 * closes the two ends with a triangulated copy of the profile. uv_scale is in
 * metres per texture tile: u runs around the profile, v runs along the curve,
 * so a road texture tiles by the metre and never stretches on a bend.
 *
 * The sweep entity's transform is owned by the module: put the road where you
 * want it by moving the spline, not the sweep. Like the primitives and
 * mesh_ops, the result is a shared mesh asset bound with IsA. The key is the
 * geometry the section generates, not where on the curve it sits: the
 * section's ring frames expressed in the frame the section starts in,
 * quantised to a millimetre and a hundredth of a radian, plus the profile,
 * the ring count, the caps and uv_scale. Two straight 120 m chunks of the
 * same track therefore share one mesh however far apart they are, and the
 * second one gets the Position3 and Rotation3 that map the first one's start
 * frame onto its own. v runs from 0 at s0, so a section tiles from its own
 * start. Generation is deferred to the next frame, because the spline an
 * entity refers to may be created after it.
 *
 *
 * AlongSpline - park an entity on a spline
 *
 * This is how pylons, lamps, signs, barrier posts and vehicles get placed.
 * The entity's Position3 is set to arc length s, offset sideways and
 * vertically in the curve's own frame; with align, its Rotation3 is set so
 * that +z points along the curve and +y follows the curve's up.
 *
 *   for i in 0..12 {
 *     "pylon_{i}" {
 *       AlongSpline: {spline: highway, s: i * 25, offset_up: -5}
 *       Cylinder: {segments: 12, length: 10}
 *       Scale3: {1.2, 1, 1.2}
 *     }
 *   }
 *
 *   car {
 *     AlongSpline: {spline: highway, s: 60, offset_right: 3, align: true}
 *     Box: {1.8, 1.4, 4.2}
 *   }
 *
 * A game system that drives vehicles sets s every frame the usual way
 * (ecs_set on AlongSpline) and the placement follows.
 *
 *
 * Sampling from C
 *
 *   float len = flecsEngine_splineLength(world, highway);
 *
 *   vec3 pos, tangent, up;
 *   if (flecsEngine_splineSample(world, highway, 240.0f, pos, tangent, up)) {
 *       // right of travel is cross(tangent, up)
 *   }
 *
 *   vec3 on_curve;
 *   float s = flecsEngine_splineNearest(world, highway, car_pos, on_curve);
 *
 * Any of the out parameters may be NULL. s is clamped to the curve on an open
 * spline and wrapped on a closed one.
 */

ECS_ENUM(FlecsSplineKind, {
    FlecsCatmullRom,
    FlecsBezier,
    FlecsLinear
});

extern ECS_COMPONENT_DECLARE(FlecsSplineKind);

/* A path through a list of control points. */
ECS_STRUCT(FlecsSpline, {
    ecs_vec_t points;      /* vec<vec3>, control points */
    FlecsSplineKind kind;
    bool closed;
    float tension;         /* CatmullRom only: 0 round .. 1 straight */
});

/* A closed 2D profile swept along a spline into a mesh. */
ECS_STRUCT(FlecsSweep, {
    ecs_entity_t spline;
    ecs_vec_t profile;     /* vec<vec2>, (right, up), closed implicitly, CCW */
    float step;            /* metres between rings, default 2 */
    float s0;              /* range along the spline, 0/0 is the whole curve */
    float s1;
    bool cap_ends;
    float uv_scale;        /* metres per texture tile, default 1 */
});

/* Position (and optionally orient) an entity at arc length s on a spline. */
ECS_STRUCT(FlecsAlongSpline, {
    ecs_entity_t spline;
    float s;               /* metres along the curve */
    float offset_right;
    float offset_up;
    bool align;            /* set Rotation3 from the curve's frame */
});

/* Length of the spline in metres, 0 if the entity carries no usable spline. */
float flecsEngine_splineLength(
    const ecs_world_t *world,
    ecs_entity_t spline);

/* Position, unit tangent and unit up at arc length s. Out parameters are
 * optional. Returns false if the entity carries no usable spline. */
bool flecsEngine_splineSample(
    const ecs_world_t *world,
    ecs_entity_t spline,
    float s,
    vec3 out_pos,
    vec3 out_tangent,
    vec3 out_up);

/* Arc length of the point on the spline closest to p. out_pos is optional. */
float flecsEngine_splineNearest(
    const ecs_world_t *world,
    ecs_entity_t spline,
    const vec3 p,
    vec3 out_pos);

void FlecsEngineSplineImport(
    ecs_world_t *world);

double flecsEngine_spline_buildSeconds(void);

#endif
