/**
 * @file geometry/mesh_gen.h
 * @brief Procedural mesh generators for the primitive shapes.
 *
 * Generated meshes are unit sized where the shape is scaled per instance.
 */

#ifndef BOX_GEOMETRY_MESH_GEN_H
#define BOX_GEOMETRY_MESH_GEN_H

#include <box/math.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CPU side mesh with separate attribute streams */
typedef struct box_mesh_data_t {
    float *positions;       /**< Three floats per vertex */
    float *normals;         /**< Three floats per vertex, unit length */
    float *uvs;             /**< Two floats per vertex in 0..1 */
    uint32_t *colors;       /**< Display encoded RGBA8 per vertex, NULL for white */
    float *sweeps;
    uint32_t *indices;      /**< Three indices per triangle */
    uint32_t vertex_count;  /**< Number of vertices */
    uint32_t index_count;   /**< Number of indices */
    vec3 bounds_min;        /**< Minimum corner of the local bounds */
    vec3 bounds_max;        /**< Maximum corner of the local bounds */
} box_mesh_data_t;

/** Release the arrays of a mesh.
 *
 * @param mesh The mesh
 */
void box_mesh_data_fini(
    box_mesh_data_t *mesh);

/** Unit cube from -0.5 to 0.5 with per face normals.
 *
 * @param out The mesh to fill
 */
void box_mesh_box(
    box_mesh_data_t *out);

/** Unit square in the XY plane facing -Z.
 *
 * @param out The mesh to fill
 */
void box_mesh_quad(
    box_mesh_data_t *out);

/** Isosceles triangle with base 1 along X and height 1 along Y, facing -Z.
 *
 * @param out The mesh to fill
 */
void box_mesh_triangle(
    box_mesh_data_t *out);

/** Right triangle with unit legs along X and Y, right angle at the origin corner, facing -Z.
 *
 * @param out The mesh to fill
 */
void box_mesh_right_triangle(
    box_mesh_data_t *out);

/** Isosceles triangle extruded along Z from -0.5 to 0.5.
 *
 * @param out The mesh to fill
 */
void box_mesh_triangle_prism(
    box_mesh_data_t *out);

/** Right triangle extruded along Z from -0.5 to 0.5.
 *
 * @param out The mesh to fill
 */
void box_mesh_right_triangle_prism(
    box_mesh_data_t *out);

/** Flat regular polygon with unit radius in the XY plane facing -Z.
 *
 * @param out The mesh to fill
 * @param sides Number of sides, at least 3
 */
void box_mesh_ngon(
    box_mesh_data_t *out,
    int32_t sides);

/** Thick line segment in the XY plane facing -Z, with square ends.
 *
 * The uvs run from 0 to 1 along the segment in U and across it in V.
 *
 * @param out The mesh to fill
 * @param x1 X of the first end
 * @param y1 Y of the first end
 * @param x2 X of the second end
 * @param y2 Y of the second end
 * @param thickness Width of the segment
 */
void box_mesh_line(
    box_mesh_data_t *out,
    float x1,
    float y1,
    float x2,
    float y2,
    float thickness);

/** Unit square in the XY plane facing -Z whose uvs show a region of a texture.
 *
 * @param out The mesh to fill
 * @param u0 Left edge of the region
 * @param v0 Bottom edge of the region
 * @param u1 Right edge of the region
 * @param v1 Top edge of the region
 * @param rotation Degrees the region is turned by around its center
 */
void box_mesh_image(
    box_mesh_data_t *out,
    float u0,
    float v0,
    float u1,
    float v1,
    float rotation);

/** Unit radius UV sphere.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around and along the sphere, at least 3
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_sphere(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth);

/** Upper half of a unit radius UV sphere, closed with a flat base at y = 0.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions, at least 3
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_hemisphere(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth);

/** Unit radius icosphere.
 *
 * @param out The mesh to fill
 * @param subdivisions Subdivision level from 0 to 5
 * @param smooth Shared vertices with smooth normals, else flat faces
 */
void box_mesh_icosphere(
    box_mesh_data_t *out,
    int32_t subdivisions,
    bool smooth);

/** Cone with unit radius base at y = -0.5 and apex at y = 0.5.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around the axis, at least 3
 * @param smooth Shared side vertices with smooth normals, else flat faces
 */
void box_mesh_cone(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth);

/** Cylinder with unit radius from y = -0.5 to y = 0.5, closed with caps.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around the axis, at least 3
 * @param smooth Shared side vertices with smooth normals, else flat faces
 */
void box_mesh_cylinder(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth);

/** Torus in the XZ plane with unit major radius.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around the major circle, at least 3
 * @param rings Subdivisions around the tube, at least 3
 * @param smooth Shared vertices with smooth normals, else flat faces
 * @param tube Tube radius relative to the major radius
 */
void box_mesh_torus(
    box_mesh_data_t *out,
    int32_t segments,
    int32_t rings,
    bool smooth,
    float tube);

/** Capsule along Y with unit radius; the cylinder part spans -length/2 .. length/2.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions, at least 3
 * @param smooth Shared vertices with smooth normals, else flat faces
 * @param length Cylinder length relative to the radius
 */
void box_mesh_capsule(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float length);

/** Hollow cylinder with unit outer radius from y = -0.5 to 0.5, closed at the ends.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around the axis, at least 3
 * @param smooth Shared side vertices with smooth normals, else flat faces
 * @param thickness Wall thickness relative to the outer radius, in 0..1
 * @param arc Sweep angle in radians, a full circle when >= 2 pi, else open ends are capped
 */
void box_mesh_tube(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float thickness,
    float arc);

/** Truncated cone from y = -0.5 with unit bottom radius to y = 0.5 with radius_top.
 *
 * @param out The mesh to fill
 * @param segments Subdivisions around the axis, at least 3
 * @param smooth Shared side vertices with smooth normals, else flat faces
 * @param radius_top Top radius relative to the bottom radius
 */
void box_mesh_frustum(
    box_mesh_data_t *out,
    int32_t segments,
    bool smooth,
    float radius_top);

/** Box with rounded edges and corners.
 *
 * @param out The mesh to fill
 * @param x Width
 * @param y Height
 * @param z Depth
 * @param radius Corner radius, clamped to half the smallest dimension
 * @param segments Subdivisions per rounded corner, at least 1
 */
void box_mesh_rounded_box(
    box_mesh_data_t *out,
    float x,
    float y,
    float z,
    float radius,
    int32_t segments);

#ifdef __cplusplus
}
#endif

#endif
