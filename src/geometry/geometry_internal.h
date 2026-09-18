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

#endif
