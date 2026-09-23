/**
 * @file box/spline.h
 * @brief Spline components: control points, sampling and the cached length table.
 *
 * A curve is an entity, so a road, an animation and a particle emitter share
 * one Spline. Setting the component rebuilds an arc length table and writes
 * SplineInfo with the length and the segment count of the curve.
 *
 * Script functions under box.spline: position, tangent and normal take a
 * parameter from 0 to 1, point_at, tangent_at and normal_at take a distance in
 * world units, and length returns the arc length.
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
 * Catmull rom and linear curves have a segment per pair of points; a bezier
 * segment is a start point, two handles and an end point shared with the next.
 * A looping curve closes back to the first point and wraps its parameter.
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
 * The profile polyline (x across, y up) is copied at even arc length and
 * stitched into a quad strip per edge, facing a quarter turn counter clockwise
 * from the profile direction: left to right faces up, closed clockwise outward.
 *
 * The result is an ordinary cached Mesh3, rebuilt when the component or its
 * curve changes, never per frame.
 */
ECS_STRUCT(SplineMesh, {
    ecs_entity_t spline;   /**< Curve to sweep along, 0 for the curve of the entity */
    ecs_vec(vec2) profile; /**< Cross section, x across and y up, empty for a flat strip */
    int32_t segments;      /**< Slices along the curve, 0 for eight per curve segment */
    float width;           /**< Scale of the cross section across the curve */
    float height;          /**< Scale of the cross section up */
    bool closed;           /**< Sweep the cross section as a ring, last point to first */
    bool smooth;           /**< Share the normals of neighbouring cross section points */
    bool gpu;              /**< Rebuild the swept vertices in the vertex shader */
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
 * moves at a constant speed. FollowPath adds Position, Rotation and
 * DynamicTransform to its entity.
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
