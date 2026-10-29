/**
 * @file box/geometry.h
 * @brief Primitive shape components; setting one selects a cached mesh and an instance scale.
 *
 * A disabled shape, 2D shape or text draws in no pass until it is enabled, then with its current
 * transform and material. Its children still draw, as flecs does not disable them.
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

/** Application authored mesh.
 *
 * Mesh3 is (OnInstantiate, Inherit): prefab instances share the prefab's value instead of
 * copying it, so editing the prefab's Mesh3 changes every instance that does not override it.
 *
 * An instance overrides the mesh with ecs_set or ecs_ensure; ecs_get_mut on an instance that
 * inherits it returns NULL. Instances lose the mesh when the prefab loses it or is deleted.
 */
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
    int32_t sides; /**< Number of sides, default 6, at least 3 */
    float radius;  /**< Circumradius */
});

/** UV sphere */
ECS_STRUCT(Sphere, {
    int32_t segments; /**< Subdivisions around the axis, default 24 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Radius */
});

/** Upper half of a UV sphere */
ECS_STRUCT(HemiSphere, {
    int32_t segments; /**< Subdivisions around the axis, default 24 */
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
    int32_t segments; /**< Subdivisions around the axis, default 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Base radius */
});

/** Cylinder along Y */
ECS_STRUCT(Cylinder, {
    int32_t segments; /**< Subdivisions around the axis, default 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Radius */
});

/** Ring in the XZ plane with major radius and tube radius */
ECS_STRUCT(Torus, {
    int32_t segments; /**< Subdivisions around the ring, default 24 */
    int32_t rings;    /**< Subdivisions around the tube, default 12 */
    bool smooth;      /**< Smooth shading */
    float radius;     /**< Major radius */
    float tube;       /**< Tube radius */
});

/** Capsule along Y, length is the cylinder part */
ECS_STRUCT(Capsule, {
    int32_t segments; /**< Subdivisions around the axis, default 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Cylinder length */
    float radius;     /**< Radius */
});

/** Hollow cylinder with wall thickness, arc limits the sweep in radians */
ECS_STRUCT(Tube, {
    int32_t segments; /**< Subdivisions around the axis, default 24 */
    bool smooth;      /**< Smooth shading */
    float length;     /**< Height */
    float radius;     /**< Outer radius */
    float thickness;  /**< Wall thickness */
    float arc;        /**< Sweep angle in radians, 0 for a full circle */
});

/** Truncated cone with separate bottom and top radius */
ECS_STRUCT(Frustum, {
    int32_t segments;    /**< Subdivisions around the axis, default 24 */
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
    int32_t segments; /**< Subdivisions per corner rounded up to even, default 4 */
});

/** Flat star polygon in the XY plane, the worked example of the shape registry */
ECS_STRUCT(Star, {
    int32_t points; /**< Points of the star, at least three */
    float inner;    /**< Inner radius relative to the outer one, default 0.45 */
    float radius;   /**< Outer radius */
});

/** Region of a parametric surface, addressed by SurfaceAttachment and box_surface_evaluate.
 *
 * Each region spans u and v in 0..1. The rims join the outer and inner wall of a partial shell
 * with thickness and rims at the u and v ends of its angle ranges.
 */
ECS_ENUM(SurfaceRegion, {
    SurfaceOuter,  /**< Outer wall */
    SurfaceInner,  /**< Inner wall: the wall of a thick shell, the holes or the Lathe inner */
    SurfaceStart,  /**< Start cap: start of a Sweep path or Lathe profile, first Loft section,
                    *   bottom of an extrusion or prism */
    SurfaceEnd,    /**< End cap, opposite of SurfaceStart */
    SurfaceUMin,   /**< Rim at u 0 */
    SurfaceUMax,   /**< Rim at u 1 */
    SurfaceVMin,   /**< Rim at v 0 */
    SurfaceVMax    /**< Rim at v 1 */
});

/** Kind of a path segment */
ECS_ENUM(PathSegmentKind, {
    PathLine,       /**< Straight line to the end, the control points are ignored */
    PathCubicBezier /**< Cubic Bezier curve to the end through control1 and control2 */
});

/** Segment of a Path2D that continues from the end of the previous segment */
ECS_STRUCT(PathSegment2D, {
    PathSegmentKind kind; /**< Line or cubic Bezier */
    vec2 end;             /**< End point */
    vec2 control1;        /**< First control point of a cubic Bezier */
    vec2 control2;        /**< Second control point of a cubic Bezier */
});

/** Segment of a Path3D that continues from the end of the previous segment */
ECS_STRUCT(PathSegment3D, {
    PathSegmentKind kind; /**< Line or cubic Bezier */
    vec3 end;             /**< End point */
    vec3 control1;        /**< First control point of a cubic Bezier */
    vec3 control2;        /**< Second control point of a cubic Bezier */
});

/** 2D path asset of a profile, contour or lathe, inherited by instances.
 *
 * A path needs at least one segment and finite points. A closed path whose last segment does
 * not end at the start gets a line back to it. Profiles sample a cubic segment in 16 steps.
 */
ECS_STRUCT(Path2D, {
    vec2 start;                     /**< Start point */
    ecs_vec(PathSegment2D) segments; /**< Segments, in order */
    bool closed;                    /**< The path loops back to its start */
});

/** 3D path asset that a Sweep follows, inherited by instances.
 *
 * A path needs at least one segment and finite points. A closed path whose last segment does
 * not end at the start gets a line back to it.
 */
ECS_STRUCT(Path3D, {
    vec3 start;                     /**< Start point */
    ecs_vec(PathSegment3D) segments; /**< Segments, in order */
    bool closed;                    /**< The path loops back to its start */
});

/** Sweeps a profile along a Path3D, with u around the profile and v along the path.
 *
 * The profile x follows a rotation minimizing frame of the path and its y the binormal. The
 * scale, taper and roll apply to the profile; taper and roll grow linearly along the path.
 */
ECS_STRUCT(Sweep, {
    ecs_entity_t path;    /**< Entity with the Path3D to follow */
    ecs_entity_t profile; /**< Entity with a Profile2D, 0 when contour is set */
    ecs_entity_t contour; /**< Entity with an open Path2D swept as an uncapped strip, 0 when
                           *   profile is set */
    vec2 scale;           /**< Scale of the profile x and y, default 1, 1 */
    float taper;          /**< Scale of the profile at the end of the path, default 1 */
    float roll;           /**< Rotation of the profile about the path at its end, in radians */
    bool cap_start;       /**< Close the start of the path, default true, not with a contour */
    bool cap_end;         /**< Close the end of the path, default true, not with a contour */
    int32_t segments;     /**< Unused for the mesh, the profile samples set the steps around */
    int32_t rings;        /**< Steps along the path, default 24, clamped to 3..256 */
});

/** Section of a Loft: a profile placed in 3D */
ECS_STRUCT(LoftSection, {
    ecs_entity_t profile; /**< Entity with a Profile2D, its x on X and its y on Z */
    vec3 offset;          /**< Position of the section, applied after the rotation */
    vec3 rotation;        /**< Rotation of the section in radians, as Rotation */
    vec2 scale;           /**< Scale of the profile x and y, default 1, 1 */
});

/** Surface through two or more sections, linearly interpolated between neighbors.
 *
 * Every section profile must have the same number of points and holes as the first. The u
 * coordinate runs around the profile and v from the first to the last section.
 */
ECS_STRUCT(Loft, {
    ecs_vec(LoftSection) sections; /**< Sections in order, at least two */
    bool cap_start;                /**< Close the first section */
    bool cap_end;                  /**< Close the last section */
    int32_t segments;              /**< Unused for the mesh, the profile samples set the steps */
    int32_t rings;                 /**< Steps from the first to the last section, default 24,
                                    *   clamped to 3..256 */
});

/** Revolves a Path2D about the Y axis, its x the radius and its y the height.
 *
 * The u coordinate runs a full turn around the axis and v along the profile. The caps close
 * the ends of the profile toward the axis, or toward the inner profile when it has one.
 */
ECS_STRUCT(Lathe, {
    ecs_entity_t profile; /**< Entity with the Path2D of the outer wall */
    ecs_entity_t inner;   /**< Entity with the Path2D of the inner wall, 0 for none */
    bool cap_start;       /**< Close the start of the profile */
    bool cap_end;         /**< Close the end of the profile */
    int32_t segments;     /**< Steps around the axis, default 32, clamped to 3..256 */
    int32_t rings;        /**< Steps along the profile, default 24, clamped to 3..256 */
});

/** Part of an ellipsoid between two longitudes and two latitudes, centered on the origin.
 *
 * Longitude, the u coordinate, turns in the XZ plane from +X toward +Z; latitude, v, rises
 * from the equator toward +Y. With thickness the shell gets an inner wall and optional rims.
 */
ECS_STRUCT(EllipsoidShell, {
    vec3 radius;           /**< Radius along X, Y and Z */
    float longitude_start; /**< Start longitude in radians */
    float longitude_end;   /**< End longitude in radians */
    float latitude_start;  /**< Start latitude in radians */
    float latitude_end;    /**< End latitude in radians */
    float thickness;       /**< Wall thickness, 0 for a single surface, clamped to
                            *   the smallest radius */
    bool rims;             /**< Close the ends of the angle ranges of a shell with thickness */
    int32_t segments;      /**< Steps along the longitude, default 32, clamped to 3..256 */
    int32_t rings;         /**< Steps along the latitude, default 24, clamped to 3..256 */
});

/** Part of a torus about the Y axis between two ring angles and two tube angles.
 *
 * The ring angle, u, turns in the XZ plane from +X toward +Z; the tube angle, v, turns around
 * the tube from its outside toward +Y. With thickness the shell gets an inner wall and rims.
 */
ECS_STRUCT(TorusShell, {
    float radius;      /**< Distance from the axis to the center of the tube */
    float tube;        /**< Radius of the tube */
    float ring_start;  /**< Start angle around the axis in radians */
    float ring_end;    /**< End angle around the axis in radians */
    float tube_start;  /**< Start angle around the tube in radians */
    float tube_end;    /**< End angle around the tube in radians */
    float thickness;   /**< Wall thickness, 0 for a single surface, clamped to the tube */
    bool rims;         /**< Close the ends of the angle ranges of a shell with thickness */
    int32_t segments;  /**< Steps around the axis, default 32, clamped to 3..256 */
    int32_t rings;     /**< Steps around the tube, default 24, clamped to 3..256 */
});

/** Prism over a strictly convex triangle or quad footprint with beveled edges.
 *
 * The footprint x lies on X and its y on Z, extruded along Y centered on the origin. Toward the
 * top the footprint shrinks by inset and shifts by offset; u runs around it and v up the side.
 */
ECS_STRUCT(BeveledPrism, {
    ecs_vec(vec2) footprint; /**< Three or four corners, in either winding */
    float depth;             /**< Height along Y */
    float inset;             /**< Distance the top edges move inward, at least 0 */
    vec2 offset;             /**< Shift of the top in X and Z */
    float bevel;             /**< Bevel size of the top and bottom edges, clamped to depth / 2 */
});

/** 2D profile asset: a closed outer Path2D with closed Path2D holes, inherited by instances */
ECS_STRUCT(Profile2D, {
    ecs_entity_t outer;          /**< Entity with the closed Path2D of the outline */
    ecs_vec(ecs_entity_t) holes; /**< Entities with the closed Path2D of each hole */
});

/** Extrudes a Profile2D along Y, centered on the origin, its x on X and its y on Z.
 *
 * The u coordinate runs around the profile and v from the bottom to the top.
 */
ECS_STRUCT(ProfileExtrusion, {
    ecs_entity_t profile; /**< Entity with the Profile2D */
    float depth;          /**< Height along Y */
    bool cap_start;       /**< Close the bottom */
    bool cap_end;         /**< Close the top */
});

/** Color and material of a region of a parametric surface */
ECS_STRUCT(SurfaceMaterial, {
    rgba color;      /**< Multiplied with the color of the shape, default opaque white */
    float metallic;  /**< Metallic of the region, replaces the shape's */
    float roughness; /**< Roughness of the region, replaces the shape's */
});

/** Materials per region of the parametric surface of the entity.
 *
 * Constructed with opaque white and the default metallic and roughness for every region.
 */
ECS_STRUCT(SurfaceMaterials, {
    SurfaceMaterial outer; /**< SurfaceOuter */
    SurfaceMaterial inner; /**< SurfaceInner */
    SurfaceMaterial rim;   /**< SurfaceUMin, SurfaceUMax, SurfaceVMin and SurfaceVMax */
    SurfaceMaterial cap;   /**< SurfaceStart and SurfaceEnd */
});

/** Places the entity on the parametric surface of the shape of another entity.
 *
 * The entity gets a ManualTransform and the engine writes its WorldTransform each frame: its Z
 * axis is the surface normal and its X axis the u tangent, followed by rotation and its Scale.
 */
ECS_STRUCT(SurfaceAttachment, {
    ecs_entity_t target;   /**< Entity with the parametric shape */
    SurfaceRegion region;  /**< Region of the surface */
    float u;               /**< First surface coordinate, 0..1 */
    float v;               /**< Second surface coordinate, 0..1 */
    float offset;          /**< Distance along the surface normal */
    vec3 rotation;         /**< Euler rotation in radians relative to the surface frame */
});

/** Number of vectors in box_surface_data_t.p */
#define BOX_SURFACE_VECTORS 24

/** Parameters of a parametric surface, evaluated on the CPU and by the shaders */
typedef struct box_surface_data_t {
    vec4 p[BOX_SURFACE_VECTORS]; /**< Parameter vectors, p[0][0] is the surface kind, 0 for none */
    vec4 *assets;                /**< Further vectors such as path segments and profile
                                  *   points, indexed from BOX_SURFACE_VECTORS, NULL for none */
    int32_t asset_count;         /**< Number of vectors in assets */
} box_surface_data_t;

/** Point on a parametric surface in the local space of the shape */
typedef struct box_surface_sample_t {
    vec3 position; /**< Position */
    vec3 normal;   /**< Unit normal pointing out of the surface */
    vec3 tangent;  /**< Unit tangent along u */
} box_surface_sample_t;

/** Evaluate a point on the parametric surface of the shape of an entity.
 *
 * @param world The world
 * @param entity The entity with the parametric shape
 * @param region The region of the surface
 * @param u The first surface coordinate, 0..1
 * @param v The second surface coordinate, 0..1
 * @param sample The sample to fill, scaled by the instance scale of the shape
 * @return True when the entity has the region and u, v lie on it, false otherwise
 */
bool box_surface_evaluate(
    const ecs_world_t *world,
    ecs_entity_t entity,
    SurfaceRegion region,
    float u,
    float v,
    box_surface_sample_t *sample);

/** Fill the parametric surface and the local bounds of a shape value.
 *
 * @param value The shape component value
 * @param data The surface parameters to fill
 * @param min The minimum corner of the bounds to fill
 * @param max The maximum corner of the bounds to fill
 */
typedef void (*box_shape_surface_fn)(
    const void *value,
    box_surface_data_t *data,
    vec3 min,
    vec3 max);

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
    float width; /**< Width across the segment, default 1 */
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
 * The area is u0, v0 to u1, v1 in texture coordinates, with v 0 at the top of the image; a
 * constructed Image shows the whole texture. The texture is multiplied
 * with the Rgb of the entity, which defaults to white for an Image.
 */
ECS_STRUCT(Image, {
    ecs_entity_t texture; /**< Entity with a TexturePath, or the texture of a Surface */
    float u0;             /**< Left edge of the area */
    float v0;             /**< Top edge of the area */
    float u1;             /**< Right edge of the area, default 1 */
    float v1;             /**< Bottom edge of the area, default 1 */
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
    vec4 *surface_assets;    /**< Surface vectors of a Sweep, Loft, Lathe or extrusion, NULL
                              *   for none */
    int32_t surface_asset_count; /**< Number of vectors in surface_assets */
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

/** Compute the mesh key of a shape value that references other entities.
 *
 * @param world The world
 * @param value The shape component value
 * @param key The key to fill
 */
typedef void (*box_shape_key_world_fn)(
    const ecs_world_t *world,
    const void *value,
    box_shape_key_t *key);

/** Generate the unit mesh of a shape value that references other entities.
 *
 * @param world The world
 * @param value The first shape value that produced the key
 * @param key The key
 * @param mesh The empty mesh to fill
 */
typedef void (*box_shape_generate_world_fn)(
    const ecs_world_t *world,
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh);

/** Fill the parametric surface and local bounds of a shape value that references other entities.
 *
 * @param world The world
 * @param value The shape component value
 * @param data The surface parameters to fill
 * @param min The minimum corner of the bounds to fill
 * @param max The maximum corner of the bounds to fill
 */
typedef void (*box_shape_surface_world_fn)(
    const ecs_world_t *world,
    const void *value,
    box_surface_data_t *data,
    vec3 min,
    vec3 max);

/** Registration of a shape component, with key or key_world and generate or generate_world */
typedef struct box_shape_desc_t {
    ecs_entity_t component;         /**< The shape component */
    box_shape_key_fn key;           /**< Key function */
    box_shape_generate_fn generate; /**< Mesh generator */
    box_shape_scale_fn scale;       /**< Scale function, NULL for a scale of one */
    box_shape_key_world_fn key_world;           /**< Key function that reads other entities,
                                                 *   used instead of key */
    box_shape_generate_world_fn generate_world; /**< Mesh generator that reads other entities,
                                                 *   used instead of generate */
    box_shape_surface_world_fn surface_world;   /**< Surface function that reads other
                                                 *   entities, NULL for none */
    box_shape_surface_fn surface;   /**< Parametric surface function, NULL for none */
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
