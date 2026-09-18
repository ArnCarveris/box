/**
 * @file box/geometry.h
 * @brief Primitive shape components; setting one selects a cached mesh and an instance scale.
 */

#ifndef BOX_GEOMETRY_H
#define BOX_GEOMETRY_H

#include "types.h"

#ifdef BOX_GEOMETRY_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Box with size x, y, z */
ECS_STRUCT(Box, {
    float x; /**< Width */
    float y; /**< Height */
    float z; /**< Depth */
});

/** Rectangle with size x, y in the XY plane */
ECS_STRUCT(Quad, {
    float x; /**< Width */
    float y; /**< Height */
});

/** Isosceles triangle with base x and height y */
ECS_STRUCT(Triangle, {
    float x; /**< Base */
    float y; /**< Height */
});

/** Right triangle with legs x and y */
ECS_STRUCT(RightTriangle, {
    float x; /**< Leg along X */
    float y; /**< Leg along Y */
});

/** Isosceles triangle extruded to depth z */
ECS_STRUCT(TrianglePrism, {
    float x; /**< Base */
    float y; /**< Height */
    float z; /**< Depth */
});

/** Right triangle extruded to depth z */
ECS_STRUCT(RightTrianglePrism, {
    float x; /**< Leg along X */
    float y; /**< Leg along Y */
    float z; /**< Depth */
});

/** Flat regular polygon */
ECS_STRUCT(NGon, {
    int32_t sides; /**< Number of sides */
    float radius;  /**< Circumradius */
});

/** UV sphere */
ECS_STRUCT(Sphere, {
    int32_t segments; /**< Subdivisions */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Upper half of a UV sphere */
ECS_STRUCT(HemiSphere, {
    int32_t segments; /**< Subdivisions */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Subdivided icosahedron */
ECS_STRUCT(IcoSphere, {
    int32_t segments; /**< Subdivision level */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Cone along Y */
ECS_STRUCT(Cone, {
    int32_t segments; /**< Subdivisions around the axis */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Base radius */
});

/** Cylinder along Y */
ECS_STRUCT(Cylinder, {
    int32_t segments; /**< Subdivisions around the axis */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Radius */
});

/** Ring in the XZ plane with major radius and tube radius */
ECS_STRUCT(Torus, {
    int32_t segments; /**< Subdivisions around the ring */
    int32_t rings;    /**< Subdivisions around the tube */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Major radius */
    float tube;       /**< Tube radius */
});

/** Capsule along Y, length is the cylinder part */
ECS_STRUCT(Capsule, {
    int32_t segments; /**< Subdivisions */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Cylinder length */
    float radius;     /**< Radius */
});

/** Hollow cylinder with wall thickness, arc limits the sweep in radians */
ECS_STRUCT(Tube, {
    int32_t segments; /**< Subdivisions around the axis */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Outer radius */
    float thickness;  /**< Wall thickness */
    float arc;        /**< Sweep angle in radians, 0 for a full circle */
});

/** Truncated cone with separate bottom and top radius */
ECS_STRUCT(Frustum, {
    int32_t segments;    /**< Subdivisions around the axis */
    bool smooth;         /**< Smooth shading */
    float length;        /**< Height */
    float radius_bottom; /**< Bottom radius */
    float radius_top;    /**< Top radius */
});

/** Box with rounded edges and corners */
ECS_STRUCT(RoundedBox, {
    float x;          /**< Width */
    float y;          /**< Height */
    float z;          /**< Depth */
    float radius;     /**< Corner radius */
    int32_t segments; /**< Subdivisions per corner */
});

/** Filled rectangle in the XY plane */
ECS_STRUCT(Rect, {
    float width;  /**< Width */
    float height; /**< Height */
});

/** Register the box.geometry module.
 *
 * @param world The world
 */
void BoxGeometryImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif
