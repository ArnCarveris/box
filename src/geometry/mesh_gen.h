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
 * @param segments Steps around the axis, half as many along it
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_gen_sphere(
    box_mesh_t *mesh,
    int32_t segments,
    bool smooth);

/** Upper half of a unit sphere, closed with a base at y = 0.
 *
 * @param mesh The mesh
 * @param segments Steps around the axis
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
 * @param segments Steps around the axis
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
