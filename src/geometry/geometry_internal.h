/**
 * @file geometry/geometry_internal.h
 * @brief Private mesh cache of the box.geometry module.
 */

#ifndef BOX_GEOMETRY_INTERNAL_H
#define BOX_GEOMETRY_INTERNAL_H

#include <box.h>
#include "mesh_gen.h"

/** Shape families the cache can generate */
typedef enum box_shape_kind_t {
    BoxShapeBox,              /**< Unit cube */
    BoxShapeQuad,             /**< Unit quad */
    BoxShapeTriangle,         /**< Isosceles triangle */
    BoxShapeRightTriangle,    /**< Right triangle */
    BoxShapeTrianglePrism,    /**< Extruded isosceles triangle */
    BoxShapeRightTrianglePrism, /**< Extruded right triangle */
    BoxShapeNGon,             /**< Regular polygon */
    BoxShapeSphere,           /**< UV sphere */
    BoxShapeHemiSphere,       /**< Half sphere */
    BoxShapeIcoSphere,        /**< Icosphere */
    BoxShapeCone,             /**< Cone */
    BoxShapeCylinder,         /**< Cylinder */
    BoxShapeTorus,            /**< Torus */
    BoxShapeCapsule,          /**< Capsule */
    BoxShapeTube,             /**< Tube */
    BoxShapeFrustum,          /**< Truncated cone */
    BoxShapeRoundedBox        /**< Rounded box */
} box_shape_kind_t;

/** Parameters that are baked into a generated mesh */
typedef struct box_mesh_key_t {
    box_shape_kind_t kind;  /**< Shape family */
    int32_t i0;             /**< First integer parameter such as segments */
    int32_t i1;             /**< Second integer parameter such as rings */
    bool smooth;            /**< Smooth shading */
    float f0;               /**< First ratio parameter */
    float f1;               /**< Second ratio parameter */
    float f2;               /**< Third ratio parameter */
    float f3;               /**< Fourth ratio parameter */
} box_mesh_key_t;

/** Look up or generate the mesh for a key.
 *
 * @param world The world
 * @param key The mesh key
 * @param bounds_min Receives the local bounds minimum
 * @param bounds_max Receives the local bounds maximum
 * @return The mesh handle
 */
uint32_t box_mesh_cache_get(
    ecs_world_t *world,
    const box_mesh_key_t *key,
    vec3 *bounds_min,
    vec3 *bounds_max);

/** Initialize the mesh cache singleton.
 *
 * @param world The world
 */
void box_mesh_cache_init(
    ecs_world_t *world);

/** Take a reference to an already uploaded custom mesh.
 *
 * @param world The world
 * @param hash Content hash of the mesh data
 * @param handle Receives the mesh handle when the mesh was found
 * @param bounds_min Receives the local bounds minimum
 * @param bounds_max Receives the local bounds maximum
 * @return True when a mesh with that hash exists, false when it must be uploaded
 */
bool box_mesh_cache_acquire(
    ecs_world_t *world,
    uint64_t hash,
    uint32_t *handle,
    vec3 *bounds_min,
    vec3 *bounds_max);

/** Upload a custom mesh and hold the first reference to it.
 *
 * @param world The world
 * @param hash Content hash of the mesh data
 * @param data The mesh, its arrays are copied
 * @return The mesh handle
 */
uint32_t box_mesh_cache_upload(
    ecs_world_t *world,
    uint64_t hash,
    const box_mesh_data_t *data);

/** Drop a reference to a custom mesh, freeing it when the last one goes away.
 *
 * @param world The world
 * @param handle The mesh handle, a handle of a primitive shape is ignored
 */
void box_mesh_cache_release(
    ecs_world_t *world,
    uint32_t handle);

/** Register the Mesh3 component hooks and observers.
 *
 * @param world The world
 */
void box_mesh3_init(
    ecs_world_t *world);

/** Look up or generate the mesh of a registered shape.
 *
 * @param world The world
 * @param key The shape key
 * @param build The generator of the shape
 * @param bounds_min Receives the local bounds minimum
 * @param bounds_max Receives the local bounds maximum
 * @return The mesh handle
 */
uint32_t box_mesh_cache_shape(
    ecs_world_t *world,
    const box_shape_key_t *key,
    box_shape_build_fn build,
    vec3 *bounds_min,
    vec3 *bounds_max);

/** Register the Shape component and the observers that install shape watchers.
 *
 * @param world The world
 */
void box_shape_init(
    ecs_world_t *world);

/** Register the Star shape through the public registration API.
 *
 * @param world The world
 */
void box_shape_example_init(
    ecs_world_t *world);

#endif
