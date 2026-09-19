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

/** Application authored mesh, uploaded to the renderer when it is set.
 *
 * Vertices are required. Normals may be empty, in which case the engine derives
 * them from the triangles, and uvs may be empty, which leaves them at zero.
 * Indices may be empty, which draws the vertices in the order they are given;
 * otherwise every index must name a vertex and their count must be a multiple
 * of three. Two entities whose data is identical share one uploaded mesh, which
 * is released when the last of them lets go of it.
 *
 * Colors are kept and validated but not uploaded yet: the vertex format of the
 * renderer has no color channel, so widening it is a separate task.
 */
ECS_STRUCT(Mesh3, {
    ecs_vec(vec3) vertices;    /**< Vertex positions */
    ecs_vec(vec3) normals;     /**< Vertex normals, empty to derive them per face */
    ecs_vec(vec2) uvs;         /**< Texture coordinates, empty for zeros */
    ecs_vec(uint32_t) indices; /**< Three indices per triangle, empty to draw in order */
    ecs_vec(rgb) colors;       /**< Per vertex color, one per vertex or empty */
});

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

/** Flat star polygon in the XY plane, the worked example of the shape registry */
ECS_STRUCT(Star, {
    int32_t points; /**< Points of the star, at least three */
    float inner;    /**< Inner radius relative to the outer one, 0 for 0.45 */
    float radius;   /**< Outer radius */
});

/** Filled rectangle in the XY plane */
ECS_STRUCT(Rect, {
    float width;  /**< Width */
    float height; /**< Height */
});

/** Key that identifies the mesh of a registered shape in the cache.
 *
 * Two shapes with the same key share one uploaded mesh, so a shape puts what
 * changes the geometry in the key and what only scales it in the instance
 * scale, exactly as the builtin shapes do.
 */
typedef struct box_shape_key_t {
    uint64_t id; /**< The shape component, which keeps the shape families apart */
    int32_t i0;  /**< First integer parameter, such as a segment count */
    int32_t i1;  /**< Second integer parameter */
    float f0;    /**< First ratio parameter */
    float f1;    /**< Second ratio parameter */
    float f2;    /**< Third ratio parameter */
    float f3;    /**< Fourth ratio parameter */
} box_shape_key_t;

/** Mesh a generator fills, in the local space the instance scale multiplies */
typedef struct box_shape_mesh_t box_shape_mesh_t;

/** Turn the value of a shape component into a mesh key and an instance scale.
 *
 * @param value The component value
 * @param key The key to fill, its id is already set
 * @param scale The instance scale to fill, it starts at one on every axis
 */
typedef void (*box_shape_key_fn)(
    const void *value,
    box_shape_key_t *key,
    vec3 *scale);

/** Generate the mesh a key describes.
 *
 * @param key The key
 * @param mesh The mesh to fill
 */
typedef void (*box_shape_build_fn)(
    const box_shape_key_t *key,
    box_shape_mesh_t *mesh);

/** Registration of a shape component, which installs its observer when it is set.
 *
 * Setting this on an entity makes the module watch the component and write a
 * RenderMesh for every entity that sets it, so a project adds a shape without
 * patching the engine. Removing it again takes the observer away and leaves the
 * meshes it already handed out alone. A component whose prefab instances should
 * share one value instead of copying it gets (OnInstantiate, Inherit) before the
 * registration, because a trait cannot be set once a query reads the component.
 */
typedef struct Shape {
    ecs_entity_t component;   /**< The component that carries the shape parameters */
    box_shape_key_fn key;     /**< Maps a component value to a key and an instance scale */
    box_shape_build_fn build; /**< Generates the mesh of a key */
} Shape;

extern ECS_COMPONENT_DECLARE(Shape);

/** Allocate the arrays of a generated mesh, which a generator calls first.
 *
 * @param mesh The mesh
 * @param vertices Exact number of vertices the generator writes
 * @param indices Exact number of indices the generator writes
 */
void box_shape_reserve(
    box_shape_mesh_t *mesh,
    int32_t vertices,
    int32_t indices);

/** Append one vertex to a generated mesh.
 *
 * @param mesh The mesh
 * @param position The position
 * @param normal The normal, unit length
 * @param u The U texture coordinate
 * @param v The V texture coordinate
 * @return The index of the new vertex
 */
uint32_t box_shape_vertex(
    box_shape_mesh_t *mesh,
    vec3 position,
    vec3 normal,
    float u,
    float v);

/** Append one triangle to a generated mesh.
 *
 * The front face is the winding whose cross product of the second and third
 * vertex around the first points away from the surface.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 */
void box_shape_triangle(
    box_shape_mesh_t *mesh,
    uint32_t a,
    uint32_t b,
    uint32_t c);

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
