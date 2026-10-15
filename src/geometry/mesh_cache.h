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
    box_shape_desc_t desc;   /**< Registration */
    ecs_size_t size;         /**< Size of the shape component */
    box_mesh_cache_t *cache; /**< Cache the shape resolves through */
} box_shape_t;

/** Mesh and instance scale an entity's shape resolved to; holds a reference to the mesh */
typedef struct ShapeMesh {
    uint32_t mesh; /**< Mesh handle, 0 for none */
    vec3 scale;    /**< Instance scale applied to the unit mesh */
} ShapeMesh;

/** Component id of ShapeMesh */
extern ECS_COMPONENT_DECLARE(ShapeMesh);

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

#ifdef __cplusplus
}
#endif

#endif
