/**
 * @file spline/spline_internal.h
 * @brief Curve evaluation, the cached arc length table and the entity lookups.
 *
 * The evaluation functions take the component, the lookups take an entity and
 * read the table the module caches on it. Other modules sample a curve through
 * the lookups so they never build a table of their own.
 */

#ifndef BOX_SPLINE_INTERNAL_H
#define BOX_SPLINE_INTERNAL_H

#include <box.h>

/** Samples the arc length table takes per curve segment */
#define BOX_SPLINE_STEPS (16)

/** Cached arc length table of a Spline, rebuilt when the curve changes */
typedef struct SplineTable {
    ecs_vec_t distance; /**< Cumulative arc length per sample, one float each */
    int32_t segments;   /**< Segments the table was built for */
    int32_t steps;      /**< Samples the table takes per segment */
    float length;       /**< Arc length of the whole curve */
} SplineTable;

extern ECS_COMPONENT_DECLARE(SplineTable);

/** One evaluated point of a curve */
typedef struct box_spline_sample_t {
    vec3 position; /**< Point on the curve */
    vec3 tangent;  /**< Unit tangent, the direction of travel */
    vec3 normal;   /**< Unit normal, the up of a cross section on the curve */
} box_spline_sample_t;

/** Number of segments the control points of a curve describe.
 *
 * @param spline The curve
 * @return The segment count, 0 when the curve has too few points
 */
int32_t box_spline_segments(
    const Spline *spline);

/** Evaluate a curve at a parameter.
 *
 * @param spline The curve
 * @param t Parameter, clamped to 0 to 1, taken modulo one when the curve loops
 * @param out Receives position, tangent and normal
 */
void box_spline_eval(
    const Spline *spline,
    float t,
    box_spline_sample_t *out);

/** Rebuild the arc length table of a curve.
 *
 * @param spline The curve
 * @param table The table to overwrite
 */
void box_spline_table_build(
    const Spline *spline,
    SplineTable *table);

/** Release the memory of an arc length table.
 *
 * @param table The table
 */
void box_spline_table_fini(
    SplineTable *table);

/** Parameter of the point at a distance along the curve.
 *
 * @param table The arc length table
 * @param distance Distance in world units from the start of the curve
 * @return The parameter from 0 to 1
 */
float box_spline_table_param(
    const SplineTable *table,
    float distance);

/** Distance along the curve of the point at a parameter.
 *
 * @param table The arc length table
 * @param t Parameter from 0 to 1
 * @return The distance in world units
 */
float box_spline_table_distance(
    const SplineTable *table,
    float t);

/** Look up the curve of an entity and the table cached on it.
 *
 * @param world The world
 * @param spline The curve entity
 * @param table_out Receives the arc length table, may be NULL
 * @return The curve, or NULL when the entity holds none
 */
const Spline* box_spline_get(
    const ecs_world_t *world,
    ecs_entity_t spline,
    const SplineTable **table_out);

/** Sample the curve of an entity at a parameter.
 *
 * @param world The world
 * @param spline The curve entity
 * @param t Parameter from 0 to 1
 * @param out Receives position, tangent and normal
 * @return true when the entity holds a curve, false when it does not
 */
bool box_spline_sample(
    const ecs_world_t *world,
    ecs_entity_t spline,
    float t,
    box_spline_sample_t *out);

/** Sample the curve of an entity at a distance along it.
 *
 * The distance is clamped to the curve, or taken modulo its length when the
 * curve loops.
 *
 * @param world The world
 * @param spline The curve entity
 * @param distance Distance in world units from the start of the curve
 * @param out Receives position, tangent and normal
 * @return true when the entity holds a curve, false when it does not
 */
bool box_spline_sample_at(
    const ecs_world_t *world,
    ecs_entity_t spline,
    float distance,
    box_spline_sample_t *out);

/** Arc length of the curve of an entity.
 *
 * @param world The world
 * @param spline The curve entity
 * @return The length in world units, 0 when the entity holds no curve
 */
float box_spline_length(
    const ecs_world_t *world,
    ecs_entity_t spline);

/** Register the script functions of the module.
 *
 * @param world The world
 * @param module The module entity the functions are registered under
 */
void box_spline_functions_init(
    ecs_world_t *world,
    ecs_entity_t module);

#endif
