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

/** Rounded edges of a Box, Cylinder, Frustum, Cone, Tube, TrianglePrism, RightTrianglePrism or
 * ProfileExtrusion. Radii are in the units of the shape, whatever its size; radii that do not
 * fit scale down together as in a CSS border-radius.
 *
 * A Box takes radius or its 12 edge radii, a round shape radius or top and bottom for its outer
 * and inner rims (a Cone has its bottom rim only), a prism radius only, and a ProfileExtrusion
 * radius or top and bottom for the edges of its caps around the outline and the holes.
 *
 * It has no effect on other shapes, which have no edges, are flat or authored, round themselves
 * (RoundedBox, BeveledPrism) or are out of scope (HemiSphere, Sweep, Loft, Lathe, the shells).
 * A non-uniform Scale still stretches the rounding, and a clamp wrap smears onto the rims.
 */
ECS_STRUCT(Bevel, {
    float radius;       /**< Radius of every edge, clamped to what fits the shape; when not 0
                         *   the other radii are ignored */
    float top_front;    /**< Box edge between the top (+Y) and the front (+Z), 0 for a sharp
                         *   edge; right is +X */
    float top_back;     /**< Box edge between the top and the back */
    float top_left;     /**< Box edge between the top and the left */
    float top_right;    /**< Box edge between the top and the right */
    float bottom_front; /**< Box edge between the bottom and the front */
    float bottom_back;  /**< Box edge between the bottom and the back */
    float bottom_left;  /**< Box edge between the bottom and the left */
    float bottom_right; /**< Box edge between the bottom and the right */
    float front_left;   /**< Vertical Box edge between the front and the left */
    float front_right;  /**< Vertical Box edge between the front and the right */
    float back_left;    /**< Vertical Box edge between the back and the left */
    float back_right;   /**< Vertical Box edge between the back and the right */
    float top;          /**< Top rims of a round shape, the cap_end edges of a ProfileExtrusion,
                         *   or the top edges of a Box whose own radius is 0 */
    float bottom;       /**< Bottom rims of a round shape, the cap_start edges of a
                         *   ProfileExtrusion, or the bottom edges of a Box whose own radius is 0 */
    int32_t segments;   /**< Subdivisions per rounded edge, rounded up to even on a Box and a
                         *   prism, default 4 */
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
    PathCubicBezier, /**< Cubic Bezier curve to the end through control1 and control2 */
    PathCatmullRom   /**< Uniform Catmull-Rom curve to the end, the control points are ignored.
                      *   Its tangents come from the anchors of the adjacent segments, which
                      *   wrap around a closed path and are the endpoint at an open end */
});

/** Segment of a Path2D that continues from the end of the previous segment */
ECS_STRUCT(PathSegment2D, {
    PathSegmentKind kind; /**< Line, cubic Bezier or Catmull-Rom */
    vec2 end;             /**< End point */
    vec2 control1;        /**< First control point of a cubic Bezier */
    vec2 control2;        /**< Second control point of a cubic Bezier */
});

/** Segment of a Path3D that continues from the end of the previous segment */
ECS_STRUCT(PathSegment3D, {
    PathSegmentKind kind; /**< Line, cubic Bezier or Catmull-Rom */
    vec3 end;             /**< End point */
    vec3 control1;        /**< First control point of a cubic Bezier */
    vec3 control2;        /**< Second control point of a cubic Bezier */
});

/** 2D path asset of a profile, contour or lathe, inherited by instances.
 *
 * A path needs at least one segment and finite points. A closed path whose last segment does
 * not end at the start gets a segment back to it: Catmull-Rom when the last segment is
 * Catmull-Rom, otherwise a line. Profiles sample a curved segment in 16 steps, or in the
 * segments of the Sweep or Loft that uses them.
 */
ECS_STRUCT(Path2D, {
    vec2 start;                     /**< Start point */
    ecs_vec(PathSegment2D) segments; /**< Segments, in order */
    bool closed;                    /**< The path loops back to its start */
});

/** 3D path asset that a Sweep follows, inherited by instances.
 *
 * A path needs at least one segment and finite points. A closed path whose last segment does
 * not end at the start gets a segment back to it: Catmull-Rom when the last segment is
 * Catmull-Rom, otherwise a line. Every segment resolves to a cubic Bezier.
 */
ECS_STRUCT(Path3D, {
    vec3 start;                     /**< Start point */
    ecs_vec(PathSegment3D) segments; /**< Segments, in order */
    bool closed;                    /**< The path loops back to its start */
});

/** Sweeps a profile along a Path3D, with u around the profile and v along the path.
 *
 * The profile x follows a rotation minimizing frame that starts at cross(up, tangent), up being
 * +Y (+X for a path along Y), and its y the binormal, so along a horizontal path the profile y
 * points down. The scale, taper and roll apply to the profile, taper and roll linearly.
 *
 * The walls map u and v by arc length, around the profile and along the path, the caps the
 * profile x and y. With a MappingObject TextureMapping they are lengths in the units of the path
 * over the mapping size instead, so a road gets a texture density in meters.
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
    int32_t segments;     /**< Steps of each curved segment of the profile, default 16,
                           *   clamped to 1..64 */
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
    int32_t segments;              /**< Steps of each curved segment of the profiles, default
                                    *   16, clamped to 1..64 */
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
 *
 * Its parametric surface ignores a Bevel, so an attachment on a cap lands on the unbeveled cap.
 */
ECS_STRUCT(ProfileExtrusion, {
    ecs_entity_t profile; /**< Entity with the Profile2D */
    float depth;          /**< Height along Y */
    bool cap_start;       /**< Close the bottom */
    bool cap_end;         /**< Close the top */
    bool smooth;          /**< Smooth shading, corners of the profile stay sharp */
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
 * The 2D shapes (Rect, Circle, Line, Polygon2D) are quads shaded with an analytic signed
 * distance, so their edges stay crisp at any scale, distance and angle; Rect, Circle and Line
 * share one instanced batch, the polygons another.
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

/** Filled 2D polygon in the XY plane from a Path2D outline, in the local space of the entity.
 *
 * The path entity holds a closed Path2D, a Profile2D with holes, or a Path3D seen from above, its
 * points sampled as profiles do, times the scale plus the offset. The fill follows the even-odd
 * rule; an open Path2D draws a stroke of the width with round joins and ends instead.
 *
 * Shaded like Rect, Circle and Line with an analytic signed distance, with their Border, Image,
 * BoxShadow, ClipChildren, Opacity, transitions and picking. An outline of more than
 * BOX_POLYGON2D_MAX_EDGES edges is not drawn; a Rect, Circle or Line of the entity draws instead.
 *
 * @par A plaza on a map at 4 units per meter, with north (-Z) up:
 * @code
 * plaza_outline {
 *   Path2D: {start: {-6, -4}, closed: true, segments: [
 *     {kind: Line, end: {6, -4}},
 *     {kind: CatmullRom, end: {7, 3}},
 *     {kind: CatmullRom, end: {0, 6}},
 *     {kind: Line, end: {-6, 3}}
 *   ]}
 * }
 * map_plaza {
 *   Polygon2D: {path: plaza_outline, scale: {4, -4}}
 *   Rgb: {200, 190, 160}
 *   Border: {width: 1.5, color: {90, 80, 60, 255}}
 * }
 * @endcode
 */
ECS_STRUCT(Polygon2D, {
    ecs_entity_t path; /**< Entity with a Path2D, a Profile2D or a Path3D */
    vec2 scale;        /**< Scale of the path x and y, default 1, 1 */
    vec2 offset;       /**< Offset added to the scaled points */
    float width;       /**< Width of the stroke of an open path, default 1 */
});

/** Most edges a Polygon2D draws with, its outline and holes together */
#define BOX_POLYGON2D_MAX_EDGES (4096)

/** Rounded corners and an inner border of the Rect, Circle or Polygon2D of the entity.
 *
 * The border lies inside the edge of the shape and the fill (the Rgb of the entity) inside the
 * border. The radius rounds the corners of a Rect and is clamped to half its smaller side.
 *
 * Transitionable: width and radius interpolate linearly, the color per channel in display
 * encoding, negative values draw as 0. Picking, layout and ClipChildren use the target values.
 */
ECS_STRUCT(Border, {
    float width;  /**< Width of the border, 0 for none */
    float radius; /**< Corner radius of a Rect */
    rgba color;   /**< Color of the border, default opaque black */
});

/** Soft shadow drawn behind the Rect, Circle or Polygon2D of the entity, like a CSS box-shadow.
 *
 * Lengths are in the units of the shape. The shadow is the outline moved by the offset and grown
 * by the spread, with a Gaussian edge of a standard deviation of half the blur.
 *
 * Its alpha is that of the color times the opacity of the entity. It is not drawn under the
 * shape, nor on a Line, a stroked Polygon2D or Text. Picking and layout ignore it.
 */
ECS_STRUCT(BoxShadow, {
    float offset_x; /**< Horizontal offset of the shadow */
    float offset_y; /**< Vertical offset of the shadow */
    float blur;     /**< Blur radius, twice the standard deviation of the edge, 0 for sharp */
    float spread;   /**< Growth of the shadow box on each side, negative to shrink it */
    rgba color;     /**< Color of the shadow, display encoded, default opaque black */
});

/** Area of a texture shown on the Rect, Circle or Polygon2D of the entity.
 *
 * The area is u0, v0 to u1, v1 in texture coordinates, with v 0 at the top of the image, over
 * the shape or the bounds of a Polygon2D outline; a constructed Image shows the whole texture.
 * The texture is multiplied with the Rgb of the entity, which defaults to white for an Image.
 */
ECS_STRUCT(Image, {
    ecs_entity_t texture; /**< Entity with a TexturePath, or the texture of a Surface */
    float u0;             /**< Left edge of the area */
    float v0;             /**< Top edge of the area */
    float u1;             /**< Right edge of the area, default 1 */
    float v1;             /**< Bottom edge of the area, default 1 */
});

/** How a part of an ImageSlice fills its area */
ECS_ENUM(SliceFill, {
    SliceStretch = 0, /**< The part stretches to its area */
    SliceTile = 1     /**< The part repeats at its own size from its start, cut at the far end */
});

/** Nine-slice scaling of the Image of a Rect, the CSS border-image model.
 *
 * The insets, in texels of the texture, cut the area of the Image into nine parts. Corners draw
 * at their size times scale, edges and center fill the rest, stretched or tiled. Corners that do
 * not fit scale down together, as in CSS. Until the texture size is known the plain Image draws.
 *
 * Rgb, Opacity, Border and ClipChildren apply as for a plain Image, TextureTransform does not,
 * and a Circle shows the plain Image. Picking onto a Surface texture maps the hit through the
 * same parts (box_image_slice_map).
 *
 * @par A wood panel from a 64 by 64 frame texture with 20 texel corners and a tiled center:
 * @code
 * template WoodPanel {
 *   prop w: f32 = 300
 *   prop h: f32 = 120
 *   LayoutItem: {width: {mode: Fixed, value: w}, height: {mode: Fixed, value: h}}
 *   Rect: {1, 1}
 *   Image: {texture: castle_ui_frame}
 *   ImageSlice: {left: 20, top: 20, right: 20, bottom: 20, edges: SliceTile,
 *     center: SliceTile}
 *   Border: {radius: 6}
 * }
 * @endcode
 */
ECS_STRUCT(ImageSlice, {
    float left;         /**< Inset of the left slices in texels of the texture, from the area */
    float top;          /**< Inset of the top slices */
    float right;        /**< Inset of the right slices */
    float bottom;       /**< Inset of the bottom slices */
    float scale;        /**< Logical pixels per texel of the corners and edges, default 1 */
    SliceFill edges;    /**< Fill of the four edges, default SliceStretch */
    SliceFill center;   /**< Fill of the center, default SliceStretch */
    bool hollow;        /**< The center is not drawn, default false */
});

/** The nine parts of a sliced Image on an element of a size (box_image_slice_resolve).
 *
 * The columns and rows are the edges of the parts in the units of the element from its top left
 * corner: x[0] is 0, x[1] the right edge of the left corners, x[2] the left edge of the right
 * corners and x[3] the width; u and v are the texture coordinates at those edges.
 */
typedef struct box_image_slice_t {
    float x[4];         /**< Column edges, 0 to the width */
    float y[4];         /**< Row edges, 0 to the height, down */
    float u[4];         /**< Texture u at the column edges */
    float v[4];         /**< Texture v at the row edges */
    float period[4];    /**< Length a tiled part repeats at, 0 for a stretched one: the top and
                         *   bottom edges along x, the left and right edges along y, the center
                         *   along x and along y */
    float scale;        /**< Units of the element per texel of the corners, after they fit */
    bool hollow;        /**< The center is not drawn */
} box_image_slice_t;

/** Resolve the nine parts of a sliced Image on an element.
 *
 * @param image The Image
 * @param slice The ImageSlice
 * @param texture_width Width of the texture in texels
 * @param texture_height Height of the texture in texels
 * @param width Width of the element
 * @param height Height of the element
 * @param out Receives the parts
 * @return Whether the image is sliced, false when the texture or the element or the area of
 * the image has no size, in which case out maps the element to the plain area
 */
bool box_image_slice_resolve(
    const Image *image,
    const ImageSlice *slice,
    float texture_width,
    float texture_height,
    float width,
    float height,
    box_image_slice_t *out);

/** Map a point of an element to the texture coordinate of its sliced Image, as the shader does.
 *
 * @param slice The resolved parts
 * @param x Distance of the point from the left of the element
 * @param y Distance of the point from the top of the element
 * @param uv Receives the texture coordinate
 * @return Whether the image is drawn there, false in the center of a hollow one
 */
bool box_image_slice_map(
    const box_image_slice_t *slice,
    float x,
    float y,
    float uv[2]);

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
    ecs_entity_t exclude;           /**< Component of entities whose shape another resolver
                                     *   sets, 0 for none */
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
