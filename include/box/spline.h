/**
 * @file box/spline.h
 * @brief Spline components: control points, sampling and the cached length table.
 *
 * A curve is an entity, so a road, an animation and a particle emitter share
 * one Spline. Setting the component rebuilds an arc length table and writes
 * SplineInfo with the length and the segment count of the curve.
 *
 * Scripts sample a curve with the functions the module registers under
 * box.spline: position(s, t), tangent(s, t) and normal(s, t) take a parameter
 * from 0 to 1, point_at(s, d), tangent_at(s, d) and normal_at(s, d) take a
 * distance in world units and length(s) returns the arc length.
 */

#ifndef BOX_SPLINE_H
#define BOX_SPLINE_H

#include "types.h"

#ifdef BOX_SPLINE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** How the control points of a spline are interpolated */
ECS_ENUM(SplineType, {
    SplineCatmullRom = 0, /**< Smooth curve through every control point */
    SplineLinear = 1,     /**< Straight segments between the control points */
    SplineBezier = 2      /**< Cubic bezier chain, three points per segment */
});

/** Curve through control points, sampled by parameter and by arc length.
 *
 * A catmull rom or linear curve has one segment between every two points, a
 * bezier takes the first point of a segment, two handles and its last point,
 * which is the first point of the next one. A curve that loops closes from the
 * last point back to the first and takes its parameter modulo one.
 */
ECS_STRUCT(Spline, {
    ecs_vec(vec3) points; /**< Control points of the curve */
    SplineType type;      /**< How the points are interpolated */
    float tension;        /**< Catmull rom tangent scale, 0.5 by default */
    bool loop;            /**< Close the curve from the last point to the first */
});

/** Measurements of a Spline, written by the engine when the curve changes */
ECS_STRUCT(SplineInfo, {
    float length;     /**< Arc length of the whole curve in world units */
    int32_t segments; /**< Number of segments the control points describe */
});

/** Cross section swept along a Spline, rebuilt as a Mesh3 when either changes.
 *
 * The profile is a polyline in the plane of the cross section, x across the
 * curve and y up, that the engine copies along the curve at even arc length and
 * stitches into a quad strip per profile edge. A strip faces a quarter turn
 * counter clockwise from the direction the profile runs, so a profile listed
 * from left to right is a road that faces up and a closed profile listed
 * clockwise is a pipe that faces outward; a road, a fence, a cable and a pipe
 * are the same sweep with another profile.
 *
 * The result is an ordinary Mesh3, so the sweep is drawn, lit, shadowed,
 * textured and collided with as any other mesh and is shared by the mesh cache
 * with every sweep that comes out the same. The entity stays a static instance:
 * the mesh is rebuilt when the component or its curve changes, never per frame.
 */
ECS_STRUCT(SplineMesh, {
    ecs_entity_t spline;   /**< Curve to sweep along, 0 for the curve of the entity */
    ecs_vec(vec2) profile; /**< Cross section, x across and y up, empty for a flat strip */
    int32_t segments;      /**< Slices along the curve, 0 for eight per curve segment */
    float width;           /**< Scale of the cross section across the curve */
    float height;          /**< Scale of the cross section up */
    bool closed;           /**< Sweep the cross section as a ring, last point to first */
    bool smooth;           /**< Share the normals of neighbouring cross section points */
    bool gpu;
});

/** What a follower does when it reaches the end of its path */
ECS_ENUM(PathMode, {
    PathLoop = 0,     /**< Start over at the beginning of the curve */
    PathPingPong = 1, /**< Turn around and travel back */
    PathOnce = 2      /**< Stop at the end of the curve */
});

/** Moves an entity along a spline by arc length, one step per frame.
 *
 * The follower is sampled at offset plus travelled, folded by the mode, so it
 * moves at speed world units per second whatever the shape of the curve. A
 * FollowPath adds Position, Rotation and DynamicTransform to its entity, which
 * makes a camera or a light follow a curve as readily as a mesh.
 */
ECS_STRUCT(FollowPath, {
    ecs_entity_t spline; /**< Curve entity to follow */
    float speed;         /**< World units per second, negative travels backwards */
    float offset;        /**< Distance along the curve the follower starts at */
    PathMode mode;       /**< What happens at the end of the curve */
    bool orient;         /**< Write Rotation so local +Z follows the tangent */
    float travelled;     /**< Distance travelled since the start, written by the engine */
});

/** Register the box.spline module.
 *
 * @param world The world
 */
void BoxSplineImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
