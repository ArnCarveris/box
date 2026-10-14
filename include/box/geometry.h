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

/** Application authored mesh. */
ECS_STRUCT(Mesh3, {
    ecs_vec(vec3) vertices;    /**< Vertex positions */
    ecs_vec(vec3) normals;     /**< Vertex normals, empty to derive them per face */
    ecs_vec(vec2) uvs;         /**< Texture coordinates, empty for zeros */
    ecs_vec(uint32_t) indices; /**< Three indices per triangle, empty to draw in order */
    ecs_vec(rgba) colors;      /**< Per vertex color and alpha, one per vertex or empty */
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
    int32_t sides; /**< Number of sides, 0 for 6 */
    float radius;  /**< Circumradius */
});

/** UV sphere */
ECS_STRUCT(Sphere, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Upper half of a UV sphere */
ECS_STRUCT(HemiSphere, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Subdivided icosahedron */
ECS_STRUCT(IcoSphere, {
    int32_t segments; /**< Subdivision level from 0 to 6 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Cone along Y */
ECS_STRUCT(Cone, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Base radius */
});

/** Cylinder along Y */
ECS_STRUCT(Cylinder, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Radius */
});

/** Ring in the XZ plane with major radius and tube radius */
ECS_STRUCT(Torus, {
    int32_t segments; /**< Subdivisions around the ring, 0 for 24 */
    int32_t rings;    /**< Subdivisions around the tube, 0 for 12 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Major radius */
    float tube;       /**< Tube radius */
});

/** Capsule along Y, length is the cylinder part */
ECS_STRUCT(Capsule, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Cylinder length */
    float radius;     /**< Radius */
});

/** Hollow cylinder with wall thickness, arc limits the sweep in radians */
ECS_STRUCT(Tube, {
    int32_t segments; /**< Subdivisions around the axis, 0 for 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Outer radius */
    float thickness;  /**< Wall thickness */
    float arc;        /**< Sweep angle in radians, 0 for a full circle */
});

/** Truncated cone with separate bottom and top radius */
ECS_STRUCT(Frustum, {
    int32_t segments;    /**< Subdivisions around the axis, 0 for 24 */
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
    int32_t segments; /**< Subdivisions per corner rounded up to even, 0 for 4 */
});

/** Flat star polygon in the XY plane, the worked example of the shape registry */
ECS_STRUCT(Star, {
    int32_t points; /**< Points of the star, at least three */
    float inner;    /**< Inner radius relative to the outer one, 0 for 0.45 */
    float radius;   /**< Outer radius */
});

/** Filled rectangle in the XY plane, centered on the origin and facing +Z.
 *
 * The 2D shapes (Rect, Circle, Line) are quads shaded with an analytic signed distance, so
 * their edges stay crisp at any scale, distance and angle; they share one instanced batch.
 *
 * They are unlit: the color is the Rgb times the albedo texture plus the emission. Overlapping
 * 2D shapes at the same depth draw over each other in the order of their batch.
 */
ECS_STRUCT(Rect, {
    float width;  /**< Width */
    float height; /**< Height */
});

/** Filled disc in the XY plane, centered on the origin and facing +Z */
ECS_STRUCT(Circle, {
    float radius; /**< Radius */
});

/** Line segment in the XY plane with square ends, in the local space of the entity */
ECS_STRUCT(Line, {
    float x1;    /**< X of the start */
    float y1;    /**< Y of the start */
    float x2;    /**< X of the end */
    float y2;    /**< Y of the end */
    float width; /**< Width across the segment, 0 for 1 */
});

/** Rounded corners and an inner border of the Rect or Circle of the entity.
 *
 * The border lies inside the edge of the shape and the fill (the Rgb of the entity) inside the
 * border. The radius rounds the corners of a Rect and is clamped to half its smaller side.
 */
ECS_STRUCT(Border, {
    float width;  /**< Width of the border, 0 for none */
    float radius; /**< Corner radius of a Rect */
    rgba color;   /**< Color of the border, default opaque black */
});

/** Area of a texture shown on the Rect or Circle of the entity.
 *
 * The area is u0, v0 to u1, v1 in texture coordinates, with v 0 at the top of the image; a u1
 * or v1 of zero is one, so a zeroed Image shows the whole texture. The texture is multiplied
 * with the Rgb of the entity, which defaults to white for an Image.
 */
ECS_STRUCT(Image, {
    ecs_entity_t texture; /**< Entity with a TexturePath */
    float u0;             /**< Left edge of the area */
    float v0;             /**< Top edge of the area */
    float u1;             /**< Right edge of the area, 0 for 1 */
    float v1;             /**< Bottom edge of the area, 0 for 1 */
});

/** Vertex of a shape mesh, laid out to be copied into a vertex buffer as is */
typedef struct box_vertex_t {
    vec3 position; /**< Position in unit shape space */
    vec3 normal;   /**< Unit length normal pointing out of the shape */
    vec2 uv;       /**< Texture coordinate in 0..1 */
} box_vertex_t;

/** CPU side triangle mesh of a shape, front faces are counter-clockwise */
typedef struct box_mesh_t {
    box_vertex_t *vertices;  /**< Vertex array */
    uint32_t *indices;       /**< Three indices per triangle */
    rgba *colors;            /**< One color per vertex, NULL when the mesh has none */
    int32_t vertex_count;    /**< Number of vertices */
    int32_t index_count;     /**< Number of indices */
    int32_t vertex_capacity; /**< Allocated vertices */
    int32_t index_capacity;  /**< Allocated indices */
    vec3 min;                /**< Minimum corner of the bounds */
    vec3 max;                /**< Maximum corner of the bounds */
} box_mesh_t;

/** Mesh parameters of a shape value, values with equal keys share one mesh */
typedef struct box_shape_key_t {
    int32_t i[4]; /**< Topology parameters such as segments and smooth */
    float f[4];   /**< Proportions that a scale cannot express, quantized */
} box_shape_key_t;

/** Compute the mesh key of a shape value; the key is zeroed before the call.
 *
 * @param value The shape component value
 * @param key The key to fill
 */
typedef void (*box_shape_key_fn)(
    const void *value,
    box_shape_key_t *key);

/** Generate the unit mesh of a key with box_mesh_vertex and box_mesh_triangle.
 *
 * @param value The first shape value that produced the key
 * @param key The key
 * @param mesh The empty mesh to fill
 */
typedef void (*box_shape_generate_fn)(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh);

/** Compute the instance scale that turns the unit mesh into the shape value.
 *
 * @param value The shape component value
 * @param scale The scale to fill
 */
typedef void (*box_shape_scale_fn)(
    const void *value,
    vec3 scale);

/** Registration of a shape component */
typedef struct box_shape_desc_t {
    ecs_entity_t component;         /**< The shape component */
    box_shape_key_fn key;           /**< Key function */
    box_shape_generate_fn generate; /**< Mesh generator */
    box_shape_scale_fn scale;       /**< Scale function, NULL for a scale of one */
} box_shape_desc_t;

/** Register a shape so that setting its component selects a cached mesh.
 *
 * Register a shape before its component is used, as adding the shape also adds ShapeMesh.
 *
 * @param world The world, which must have imported box.geometry
 * @param desc The shape registration
 * @return Zero on success, nonzero when the registration is invalid, a duplicate or too late
 */
int box_shape_register(
    ecs_world_t *world,
    const box_shape_desc_t *desc);

/** Append a vertex to a mesh.
 *
 * @param mesh The mesh
 * @param position The position
 * @param normal The unit length normal
 * @param u The U texture coordinate
 * @param v The V texture coordinate
 * @return The index of the vertex
 */
int32_t box_mesh_vertex(
    box_mesh_t *mesh,
    const vec3 position,
    const vec3 normal,
    float u,
    float v);

/** Append a triangle, wound counter-clockwise around the normals of its vertices.
 *
 * @param mesh The mesh
 * @param a First vertex index
 * @param b Second vertex index
 * @param c Third vertex index
 */
void box_mesh_triangle(
    box_mesh_t *mesh,
    int32_t a,
    int32_t b,
    int32_t c);

/** Round a proportion so that values that look the same share a mesh key.
 *
 * @param value The proportion
 * @return The value rounded to a multiple of 1/4096
 */
float box_shape_quantize(
    float value);

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
