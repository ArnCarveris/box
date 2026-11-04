/**
 * @file geometry/mesh_cache.h
 * @brief Private mesh cache of box.geometry: shape registry, cached meshes and resolved shapes.
 */

#ifndef BOX_GEOMETRY_MESH_CACHE_H
#define BOX_GEOMETRY_MESH_CACHE_H

#include <box.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mesh cache of a world, owned by the type hooks of ShapeMesh */
typedef struct box_mesh_cache_t box_mesh_cache_t;

/** Registered shape */
typedef struct box_shape_t {
    const ecs_world_t *world; /**< World passed to the world-aware shape callbacks */
    box_shape_desc_t desc;   /**< Registration */
    ecs_size_t size;         /**< Size of the shape component */
    box_mesh_cache_t *cache; /**< Cache the shape resolves through */
} box_shape_t;

/** Mesh and instance scale an entity's shape resolved to; holds a reference to the mesh */
typedef struct ShapeMesh {
    uint32_t mesh; /**< Mesh handle, 0 for none */
    uint32_t render_mesh; /**< Mesh with surface positions resolved on the CPU, 0 for none */
    vec3 scale;    /**< Instance scale applied to the unit mesh */
    box_surface_data_t surface; /**< Parameters of a parametric surface, p[0][0] is 0 for none */
    vec3 min;      /**< Minimum of the local bounds of a parametric surface */
    vec3 max;      /**< Maximum of the local bounds of a parametric surface */
} ShapeMesh;

/** Component id of ShapeMesh */
extern ECS_COMPONENT_DECLARE(ShapeMesh);

/** Value of the Bevel shape: the Box and the Bevel of an entity */
typedef struct box_bevel_value_t {
    Box box;      /**< Size of the box */
    Bevel bevel;  /**< Edge radii */
} box_bevel_value_t;

void box_geometry_validation_init(ecs_world_t *world);

/** Create an empty mesh cache.
 *
 * @return The cache
 */
box_mesh_cache_t* box_mesh_cache_new(void);

/** Free a mesh cache with its shapes and meshes.
 *
 * @param ctx The cache
 */
void box_mesh_cache_free(
    void *ctx);

/** Get the mesh cache of a world.
 *
 * @param world The world
 * @return The cache, NULL when box.geometry was not imported
 */
box_mesh_cache_t* box_mesh_cache_of(
    const ecs_world_t *world);

/** Look up or generate the mesh of a shape value and take a reference to it.
 *
 * @param cache The cache
 * @param shape The shape
 * @param value The shape component value
 * @return The mesh handle
 */
uint32_t box_mesh_cache_acquire(
    box_mesh_cache_t *cache,
    const box_shape_t *shape,
    const void *value);

/** Take another reference to a mesh.
 *
 * @param cache The cache
 * @param handle The mesh handle, 0 is ignored
 */
void box_mesh_cache_keep(
    box_mesh_cache_t *cache,
    uint32_t handle);

/** Drop a reference to a mesh, freeing it with the last one.
 *
 * @param cache The cache
 * @param handle The mesh handle, 0 is ignored
 */
void box_mesh_cache_release(
    box_mesh_cache_t *cache,
    uint32_t handle);

/** Take a reference to a mesh for a module that follows it, such as the renderer that uploaded
 * it or picking that built its triangle hierarchy, as opposed to a shape that uses it.
 *
 * Each such module drops its own hold once box_mesh_cache_users reports no users; the mesh is
 * freed with the last reference, whichever module drops it.
 *
 * @param cache The cache
 * @param handle The mesh handle, 0 is ignored
 */
void box_mesh_cache_hold(
    box_mesh_cache_t *cache,
    uint32_t handle);

/** Drop a hold taken with box_mesh_cache_hold, freeing the mesh with its last reference.
 *
 * @param cache The cache
 * @param handle The mesh handle, 0 is ignored
 */
void box_mesh_cache_unhold(
    box_mesh_cache_t *cache,
    uint32_t handle);

/** Get the number of references to a mesh that are not holds: the shapes that use it.
 *
 * @param cache The cache
 * @param handle The mesh handle
 * @return The number of users, 0 when the handle is not alive or only held
 */
int32_t box_mesh_cache_users(
    const box_mesh_cache_t *cache,
    uint32_t handle);

/** Get a cached mesh.
 *
 * @param cache The cache
 * @param handle The mesh handle
 * @return The mesh, NULL when the handle is not alive
 */
const box_mesh_t* box_mesh_cache_get(
    const box_mesh_cache_t *cache,
    uint32_t handle);

/** Get the number of meshes alive in the cache.
 *
 * @param cache The cache
 * @return The mesh count
 */
int32_t box_mesh_cache_count(
    const box_mesh_cache_t *cache);

/** Add a shape to the registry of a cache.
 *
 * @param cache The cache
 * @param desc The shape registration
 * @return The shape, NULL when the component is already registered
 */
box_shape_t* box_mesh_cache_add_shape(
    box_mesh_cache_t *cache,
    const box_shape_desc_t *desc);

/** Find a registered shape.
 *
 * @param cache The cache
 * @param component The shape component
 * @return The shape, NULL when the component is not registered
 */
box_shape_t* box_mesh_cache_shape(
    const box_mesh_cache_t *cache,
    ecs_entity_t component);

/** Resolve a shape value to a mesh and an instance scale and write it to the ShapeMesh of an
 * entity, releasing the mesh it had.
 *
 * @param world The world
 * @param stage The stage for structural changes
 * @param shape The shape
 * @param e The entity
 * @param value The shape value
 */
void box_shape_apply(
    ecs_world_t *world,
    ecs_world_t *stage,
    const box_shape_t *shape,
    ecs_entity_t e,
    const void *value);

/** Check whether a Bevel rounds no edge, so that its Box resolves as a plain Box.
 *
 * @param bevel The bevel
 * @return True when every radius that applies is 0, negative or not finite
 */
bool box_bevel_flat(
    const Bevel *bevel);

/** Create the observers that resolve entities with a Box and a Bevel.
 *
 * @param world The world
 * @param bevel The cached shape of the Bevel, whose value is a box_bevel_value_t
 */
void box_geometry_bevel_init(
    ecs_world_t *world,
    box_shape_t *bevel);

/** Free the arrays of a mesh.
 *
 * @param mesh The mesh
 */
void box_mesh_fini(
    box_mesh_t *mesh);

/** Compute the bounds of a mesh from its vertices.
 *
 * @param mesh The mesh
 */
void box_mesh_bounds(
    box_mesh_t *mesh);

/** Replace a mesh with unshared vertices per triangle that carry the face normal.
 *
 * @param mesh The mesh
 */
void box_mesh_facet(
    box_mesh_t *mesh);

/** Register the built-in 3D shapes.
 *
 * @param world The world
 */
void box_geometry_shapes_init(
    ecs_world_t *world);

/** Register the Star shape through the public registration API.
 *
 * @param world The world
 */
void box_geometry_star_init(
    ecs_world_t *world);

uint32_t box_mesh_cache_resolve(box_mesh_cache_t *cache, const ShapeMesh *shape);
void box_shape_mesh_release(box_mesh_cache_t *cache, const ShapeMesh *shape);
void box_geometry_surfaces_init(ecs_world_t *world);
void box_surface_build_frames(const box_surface_data_t *data, vec4 *frames, int steps);

/** Store in the w of each sweep frame the path parameter at the arc length fraction of the frame
 * (frame j of steps sits at fraction j / steps), the inverse of the cumulative length table of
 * the path sampled at the frame steps, and return the length of the path.
 *
 * @param data The surface data with the path packed
 * @param frames The frames from box_surface_build_frames
 * @param steps The number of frame steps
 * @param length Receives the length of the path
 */
void box_surface_build_lengths(const box_surface_data_t *data, vec4 *frames, int steps,
    float *length);

/** Path parameter of a Sweep at a fraction of the length of its path.
 *
 * @param data The surface data
 * @param s The fraction of the path length, 0..1
 * @return The path parameter, 0..1
 */
float box_surface_sweep_parameter(const box_surface_data_t *data, float s);

/** Find the segment boundaries of a Sweep path where its direction jumps, so that a ring can be
 * put on each.
 *
 * @param data The surface data
 * @param parameters Receives the path parameter of each corner, room for the segment count
 * @param fractions Receives the length fraction of each corner, room for the segment count
 * @return The number of corners, in order along the path
 */
int box_surface_sweep_corners(const box_surface_data_t *data, float *parameters,
    float *fractions);

/** Length of the edge of a Sweep profile from a point to the next, with the Sweep scale.
 *
 * @param data The surface data
 * @param point The index of the point in the profile
 * @return The length of the edge, 0 for the last point of an open contour
 */
float box_surface_profile_edge(const box_surface_data_t *data, int point);

/** Length around the outer ring or the hole rings of a Sweep profile, with the Sweep scale.
 *
 * @param data The surface data
 * @param inner 1 for the holes, 0 for the outer ring
 * @return The length
 */
float box_surface_profile_length(const box_surface_data_t *data, int inner);

/** Profile parameter of a Sweep, as surface_boundary takes it, at a fraction of the length
 * around the outer ring or the hole rings of the profile.
 *
 * @param data The surface data
 * @param s The fraction of the length, 0..1
 * @param inner 1 for the holes, 0 for the outer ring
 * @return The profile parameter, 0..1
 */
float box_surface_profile_parameter(const box_surface_data_t *data, float s, int inner);
void box_surface_sample(const box_surface_data_t *data, int region,
    float u, float v, box_surface_sample_t *sample);
void box_surface_vertex(const ShapeMesh *shape, const box_vertex_t *vertex, vec3 position);
void box_surface_materials_apply(const ecs_world_t *world, ecs_entity_t e, ShapeMesh *shape);
void box_surface_materials_init(ecs_world_t *world);
void box_geometry_profiles_init(ecs_world_t *world);
void box_geometry_attachments_init(ecs_world_t *world);
bool box_profile_contains(const box_surface_data_t *d, float x, float z);
bool box_surface_enclosed(const ShapeMesh *shape, const box_mesh_t *mesh, const vec3 point);

#ifdef __cplusplus
}
#endif

#endif
