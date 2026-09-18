/**
 * @file geometry/mesh_gen_internal.h
 * @brief Shared mesh building helpers used by the mesh generators.
 */

#ifndef BOX_GEOMETRY_MESH_GEN_INTERNAL_H
#define BOX_GEOMETRY_MESH_GEN_INTERNAL_H

#include "mesh_gen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Pi as a float constant, used instead of the possibly undefined M_PI */
#define BOX_MESH_PI (3.14159265358979323846f)

/** Full turn as a float constant */
#define BOX_MESH_TAU (BOX_MESH_PI * 2.0f)

/** Write cursor for filling a mesh whose vertex and index counts are known up front */
typedef struct box_mesh_builder_t {
    box_mesh_data_t *mesh;  /**< Mesh being filled */
    uint32_t next_vertex;   /**< Next vertex slot to write */
    uint32_t next_index;    /**< Next index slot to write */
} box_mesh_builder_t;

/** Allocate the mesh arrays and reset the write cursor.
 *
 * @param builder The builder
 * @param mesh The mesh to allocate and fill
 * @param vertex_count Exact number of vertices the generator will write
 * @param index_count Exact number of indices the generator will write
 */
void box_mesh_builder_init(
    box_mesh_builder_t *builder,
    box_mesh_data_t *mesh,
    uint32_t vertex_count,
    uint32_t index_count);

/** Append one vertex.
 *
 * @param builder The builder
 * @param position The position
 * @param normal The normal
 * @param u The U texture coordinate
 * @param v The V texture coordinate
 * @return The index of the new vertex
 */
uint32_t box_mesh_builder_vertex(
    box_mesh_builder_t *builder,
    vec3 position,
    vec3 normal,
    float u,
    float v);

/** Append one triangle.
 *
 * @param builder The builder
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 */
void box_mesh_builder_triangle(
    box_mesh_builder_t *builder,
    uint32_t a,
    uint32_t b,
    uint32_t c);

/** Read back the position of a vertex already written to the builder.
 *
 * @param builder The builder
 * @param index The vertex index
 * @return The position
 */
vec3 box_mesh_builder_position(
    const box_mesh_builder_t *builder,
    uint32_t index);

/** Compute bounds_min and bounds_max from the written positions.
 *
 * @param builder The builder
 */
void box_mesh_builder_finish(
    box_mesh_builder_t *builder);

/** Turn a shared, indexed mesh into a fully duplicated mesh with flat face normals.
 *
 * @param in The shared mesh
 * @param out The flat mesh to fill
 */
void box_mesh_facet(
    const box_mesh_data_t *in,
    box_mesh_data_t *out);

/** Clamp an integer to a minimum.
 *
 * @param value The value
 * @param min The minimum
 * @return The clamped value
 */
int32_t box_mesh_clamp_min_i32(
    int32_t value,
    int32_t min);

/** Build a flat disc cap, a center vertex fanned to a ring of rim vertices.
 *
 * @param builder The builder
 * @param radius The disc radius
 * @param y The height of the disc
 * @param sign +1.0 for a normal pointing up, -1.0 for down
 * @param segments Number of rim vertices
 */
void box_mesh_disc_cap(
    box_mesh_builder_t *builder,
    float radius,
    float y,
    float sign,
    int32_t segments);

/** Build an annulus cap, a ring shaped strip between two radii.
 *
 * @param builder The builder
 * @param inner_radius The inner radius
 * @param outer_radius The outer radius
 * @param y The height of the cap
 * @param sign +1.0 for a normal pointing up, -1.0 for down
 * @param segments Number of steps around the ring
 * @param arc Sweep angle in radians
 */
void box_mesh_annulus_cap(
    box_mesh_builder_t *builder,
    float inner_radius,
    float outer_radius,
    float y,
    float sign,
    int32_t segments,
    float arc);

/** Build the straight lateral wall of a cone, cylinder or frustum around Y.
 *
 * @param builder The builder
 * @param radius_bottom Radius at y_bottom
 * @param radius_top Radius at y_top
 * @param y_bottom Bottom height
 * @param y_top Top height
 * @param segments Steps around the axis
 * @param outward True when the normal points away from the axis
 * @param arc Sweep angle in radians
 */
void box_mesh_cone_wall(
    box_mesh_builder_t *builder,
    float radius_bottom,
    float radius_top,
    float y_bottom,
    float y_top,
    int32_t segments,
    bool outward,
    float arc);

/** Build the curved shell of a hemisphere without its base.
 *
 * @param builder The builder
 * @param center Center of the sphere the hemisphere is cut from
 * @param radius The radius
 * @param segments Subdivisions around and along the dome
 * @param upper True to build the dome above the center, false for below
 * @param v_min V texture coordinate at the rim
 * @param v_max V texture coordinate at the pole
 */
void box_mesh_hemisphere_shell(
    box_mesh_builder_t *builder,
    vec3 center,
    float radius,
    int32_t segments,
    bool upper,
    float v_min,
    float v_max);

#ifdef __cplusplus
}
#endif

#endif
