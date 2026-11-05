/**
 * @file geometry/mesh_gen.h
 * @brief Unit mesh generators of the built-in shapes; sizes are applied as instance scale.
 */

#ifndef BOX_GEOMETRY_MESH_GEN_H
#define BOX_GEOMETRY_MESH_GEN_H

#include <box.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pi as a float */
#define BOX_PI (3.14159265358979323846f)

/** Point of a profile that is revolved around the Y axis */
typedef struct box_lathe_point_t {
    float r;  /**< Distance to the axis */
    float y;  /**< Height */
    float nr; /**< Radial component of the normal */
    float ny; /**< Vertical component of the normal */
    float v;  /**< V texture coordinate */
} box_lathe_point_t;

/** Revolve a profile around the Y axis; rows on the axis become triangle fans.
 *
 * @param mesh The mesh
 * @param points The profile
 * @param count Number of profile points
 * @param slices Steps around the axis
 * @param arc Sweep angle in radians
 * @param planar True to project UVs from above instead of wrapping them around the axis
 */
void box_mesh_lathe(
    box_mesh_t *mesh,
    const box_lathe_point_t *points,
    int32_t count,
    int32_t slices,
    float arc,
    bool planar);

/** Revolve a profile around the Y axis in flat facets, faceted around the axis only.
 *
 * Each facet column gets its own vertices, whose normals stay smooth along the profile: the
 * normal of a planar strip at the radius times cos(half a step) in the middle of the column.
 *
 * @param mesh The mesh
 * @param points The profile, with smooth normals
 * @param count Number of profile points
 * @param slices Steps around the axis
 * @param arc Sweep angle in radians
 */
void box_mesh_lathe_facets(
    box_mesh_t *mesh,
    const box_lathe_point_t *points,
    int32_t count,
    int32_t slices,
    float arc);

/** Rim of a beveled round shape, the order of box_bevel_round_t.rims */
typedef enum box_bevel_rim_t {
    BoxRimBottomOuter, /**< Bottom rim of the outer wall */
    BoxRimTopOuter,    /**< Top rim of the outer wall */
    BoxRimTopInner,    /**< Top rim of the inner wall of a Tube */
    BoxRimBottomInner, /**< Bottom rim of the inner wall of a Tube */
    BoxRimCount        /**< Number of rims */
} box_bevel_rim_t;

/** Beveled round shape in the units of its scale radius: the instance scale is (1, length, 1)
 * times that radius.
 */
typedef struct box_bevel_round_t {
    int32_t segments; /**< Steps around the axis */
    int32_t steps;    /**< Steps per rounded rim */
    bool smooth;      /**< Smooth normals around the axis, else flat facets */
    float length;     /**< Height */
    float bottom;     /**< Outer radius at the bottom */
    float top;        /**< Outer radius at the top */
    float inner;      /**< Inner radius of a Tube, 0 for a solid shape */
    float arc;        /**< Sweep of a Tube in radians, 0 for a full turn */
    float rims[BoxRimCount]; /**< Radius per rim, 0 for a sharp rim or no rim */
} box_bevel_round_t;

/** Scale down the rim radii of a beveled round shape that do not fit its cross section, and
 * zero the radii of rims that are not edges.
 *
 * The setbacks of the two rims of each side, radius * tan(turn / 2), must fit its length; when
 * they do not, every radius scales down by the smallest ratio.
 *
 * @param shape The shape, whose rims are clamped
 */
void box_bevel_round_fit(
    box_bevel_round_t *shape);

/** Round shape with rounded rims along Y from -0.5 to 0.5: a Cylinder, Frustum, Cone or Tube.
 *
 * The cross section is built in the units of the shape, with circular rims tangent to their
 * sides, and mapped to the unit mesh, so that the rims stay circular under the scale.
 *
 * The walls keep the v of the unbeveled shape, the rims continue it by arc length, and the flat
 * parts of the caps keep the planar projection over the full radius.
 *
 * @param mesh The mesh
 * @param shape The shape, fitted with box_bevel_round_fit
 */
void box_mesh_gen_bevel_round(
    box_mesh_t *mesh,
    const box_bevel_round_t *shape);

/** Triangle prism with every edge rounded, extruded along Z from -0.5 to 0.5: the prism shrunk
 * by the radius and grown back by a sphere of that radius.
 *
 * The flat faces keep the coordinates of box_mesh_gen_triangle_prism. The strips split at 45
 * degrees and halfway around the vertical edges, each half continuing its face by arc length.
 *
 * @param mesh The mesh
 * @param right True for a right triangle with its right angle at -0.5, -0.5
 * @param size Size along X, Y and Z, relative to the largest
 * @param radius Radius relative to the largest size, clamped to the inradius and half of Z;
 *   within 0.1% of a limit it takes the limit, so that the faces it collapses collapse exactly
 * @param segments Steps per quarter turn of a rounded edge, rounded up to even
 */
void box_mesh_gen_bevel_prism(
    box_mesh_t *mesh,
    bool right,
    const vec3 size,
    float radius,
    int32_t segments);

/** Texture coordinates of a point on a face of a box, projected onto the face.
 *
 * On the side faces (k 0 and 2) u runs along the horizontal tangent seen from outside, up × n,
 * and v down toward -y. The top face has u along +z and v along -x, the bottom face u along -z
 * and v along -x. No face is mirrored seen from outside.
 *
 * @param k Axis of the face normal
 * @param sign Direction of the face normal, 1 or -1
 * @param pos The point
 * @param h Half sizes of the box
 * @param out The texture coordinates, 0..1 over the face, clamped
 */
void box_mesh_box_face_uv(
    int32_t k,
    float sign,
    const vec3 pos,
    const vec3 h,
    vec2 out);

/** Cube from -0.5 to 0.5.
 *
 * @param mesh The mesh
 */
void box_mesh_gen_box(
    box_mesh_t *mesh);

/** Square from -0.5 to 0.5 in the XY plane facing +Z.
 *
 * @param mesh The mesh
 */
void box_mesh_gen_quad(
    box_mesh_t *mesh);

/** Triangle in the XY plane facing +Z within -0.5 to 0.5.
 *
 * @param mesh The mesh
 * @param right True for a right triangle with its right angle at -0.5, -0.5
 */
void box_mesh_gen_triangle(
    box_mesh_t *mesh,
    bool right);

/** Triangle extruded along Z from -0.5 to 0.5.
 *
 * @param mesh The mesh
 * @param right True for a right triangle with its right angle at -0.5, -0.5
 */
void box_mesh_gen_triangle_prism(
    box_mesh_t *mesh,
    bool right);

/** Regular polygon with unit circumradius in the XY plane facing +Z.
 *
 * @param mesh The mesh
 * @param sides Number of sides
 */
void box_mesh_gen_ngon(
    box_mesh_t *mesh,
    int32_t sides);

/** UV sphere with unit radius.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis and along each half, from pole to equator
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_gen_sphere(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth);

/** Upper half of a unit sphere, closed with a base at y = 0.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis and from the pole to the base
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_gen_hemisphere(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth);

/** Subdivided icosahedron with unit radius.
 *
 * @param mesh The mesh
 * @param level Subdivision level
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_gen_icosphere(
    box_mesh_t *mesh,
    int32_t level,
    bool smooth);

/** Frustum along Y from -0.5 to 0.5, a cone or cylinder for special radii.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis
 * @param smooth Smooth side normals, else flat faces
 * @param bottom Radius at y = -0.5
 * @param top Radius at y = 0.5
 */
void box_mesh_gen_frustum(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float bottom,
    float top);

/** Torus around the Y axis with unit major radius.
 *
 * @param mesh The mesh
 * @param segments Steps around the ring
 * @param rings Steps around the tube
 * @param smooth Shared vertices with smooth normals, else flat faces
 * @param tube Tube radius relative to the major radius
 */
void box_mesh_gen_torus(
    box_mesh_t *mesh,
    int32_t segments,
    int32_t rings,
    bool smooth,
    float tube);

/** Capsule along Y with unit radius, the cylinder part spans -length/2 to length/2.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis and from each pole to the cylinder
 * @param smooth Shared vertices with smooth normals, else flat faces
 * @param length Cylinder length relative to the radius
 */
void box_mesh_gen_capsule(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float length);

/** Hollow cylinder with unit outer radius along Y from -0.5 to 0.5.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis
 * @param smooth Smooth wall normals, else flat faces
 * @param inner Inner radius relative to the outer radius
 * @param arc Sweep angle in radians, open ends are closed when below a full turn
 */
void box_mesh_gen_tube(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth,
    float inner,
    float arc);

/** Box with rounded edges and corners centered on the origin.
 *
 * @param mesh The mesh
 * @param size Size along X, Y and Z
 * @param radius Corner radius, at most half the smallest size
 * @param segments Steps per rounded corner, rounded up to even
 */
void box_mesh_gen_rounded_box(
    box_mesh_t *mesh,
    const vec3 size,
    float radius,
    int32_t segments);

/** Number of edges of a box */
#define BOX_BEVEL_EDGES (12)

/** Box with a radius per edge centered on the origin, blending the corners between the edges.
 *
 * The radii are in the order of the members of Bevel: top_front, top_back, top_left, top_right,
 * bottom_front, bottom_back, bottom_left, bottom_right, front_left, front_right, back_left and
 * back_right.
 *
 * Radii that do not fit are scaled down together: along each axis, the corners at the two ends
 * of an edge take the largest radius of their edges across that axis, and leave a sliver flat.
 *
 * @param mesh The mesh
 * @param size Size along X, Y and Z
 * @param radii Radius per edge, 0 for a sharp edge
 * @param segments Steps per rounded edge, rounded up to even
 */
void box_mesh_gen_bevel(
    box_mesh_t *mesh,
    const vec3 size,
    const float *radii,
    int32_t segments);

/** Convert an authored mesh, deriving face normals when it has none.
 *
 * @param mesh The mesh
 * @param src The authored mesh
 */
void box_mesh_gen_mesh3(
    box_mesh_t *mesh,
    const Mesh3 *src);

#ifdef __cplusplus
}
#endif

#endif
