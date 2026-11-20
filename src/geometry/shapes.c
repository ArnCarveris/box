/**
 * @file geometry/shapes.c
 * @brief Key, generator and scale functions of the built-in 3D shapes.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"
#include <math.h>
#include <stddef.h>

/** Largest number of segments around an axis */
#define BOX_MAX_SEGMENTS (512)

/** Segments around the axis of a constructed round shape */
#define BOX_SEGMENTS (24)

/** Sides of a constructed NGon */
#define BOX_NGON_SIDES (6)

/** Subdivisions around the tube of a constructed Torus */
#define BOX_TORUS_RINGS (12)

/** Subdivisions per corner of a constructed RoundedBox */
#define BOX_ROUNDED_BOX_SEGMENTS (4)

/** Apply the limits to a segment count.
 *
 * @param value The authored count
 * @return The count to generate with, from 3 to BOX_MAX_SEGMENTS
 */
static int32_t box_segments(
    int32_t value)
{
    int32_t result = value < 3 ? 3 : value;

    return result > BOX_MAX_SEGMENTS ? BOX_MAX_SEGMENTS : result;
}

int32_t box_shape_lod_count(
    int32_t count,
    int32_t floor,
    int32_t level)
{
    for (int32_t i = 0; i < level; i ++) {
        int32_t half = count / 2;
        count = half > floor ? half : (count < floor ? count : floor);
    }

    return count;
}

/** Get the subdivisions of a level of detail of a count that the generator rounds up to even:
 * the even count of level 0 halved per level and rounded up to even, at least 2.
 *
 * @param segments The count of level 0, from 1 to 64
 * @param level The level
 * @return The count of the level, segments itself while it generates the mesh of level 0
 */
static int32_t box_shape_lod_even(
    int32_t segments,
    int32_t level)
{
    int32_t even = 2 * ((segments + 1) / 2 < 1 ? 1 : (segments + 1) / 2);
    int32_t count = even;
    for (int32_t i = 0; i < level; i ++) {
        count = box_shape_lod_count(count, 2, 1);
        count += count & 1;
    }

    return count == even ? segments : count;
}

/** Construct shape values whose first member is a count, set to the count in the context.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info, whose context is the count
 */
static void box_shape_count_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    int32_t i;
    ecs_os_memset(ptr, 0, ti->size * count);
    for (i = 0; i < count; i ++) {
        *(int32_t*)ECS_ELEM(ptr, ti->size, i) = (int32_t)(intptr_t)ti->hooks.ctx;
    }
}

/** Construct Torus values with the documented subdivisions.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_torus_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Torus *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Torus){ .segments = BOX_SEGMENTS, .rings = BOX_TORUS_RINGS };
    }
}

/** Construct RoundedBox values with the documented corner subdivisions.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_rounded_box_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RoundedBox *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (RoundedBox){ .segments = BOX_ROUNDED_BOX_SEGMENTS };
    }
}

/** Construct Bevel values with the documented subdivisions per edge.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_bevel_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Bevel *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Bevel){ .segments = BOX_ROUNDED_BOX_SEGMENTS };
    }
}

/** Give a shape whose first member is a count a constructor that sets it.
 *
 * @param world The world
 * @param component The shape component
 * @param value The count
 */
static void box_shape_count_hooks(
    ecs_world_t *world,
    ecs_entity_t component,
    int32_t value)
{
    ecs_set_hooks_id(world, component, &(ecs_type_hooks_t){
        .ctor = box_shape_count_ctor,
        .ctx = (void*)(intptr_t)value
    });
}

/** Divide two sizes, returning a fallback for a divisor of zero or less.
 *
 * @param num The numerator
 * @param den The denominator
 * @param fallback The result for a divisor of zero or less
 * @return The ratio
 */
static float box_ratio(
    float num,
    float den,
    float fallback)
{
    return den > 0.0f ? num / den : fallback;
}

/** Key of a shape without mesh parameters.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_none(
    const void *value,
    box_shape_key_t *key)
{
    (void)value;
    (void)key;
}

/** Scale of a shape with the layout of Box.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_xyz(
    const void *value,
    vec3 scale)
{
    const Box *v = value;
    glm_vec3_copy((vec3){ v->x, v->y, v->z }, scale);
}

/** Scale of a flat shape with the layout of Quad.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_xy(
    const void *value,
    vec3 scale)
{
    const Quad *v = value;
    glm_vec3_copy((vec3){ v->x, v->y, 1.0f }, scale);
}

/** Generate the Box mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_box(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_box(mesh);
}

/** Generate the Quad mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_quad(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_quad(mesh);
}

/** Generate the Triangle mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_triangle(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_triangle(mesh, false);
}

/** Generate the RightTriangle mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_right_triangle(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_triangle(mesh, true);
}

/** Generate the TrianglePrism mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_triangle_prism(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_triangle_prism(mesh, false);
}

/** Generate the RightTrianglePrism mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_right_triangle_prism(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    (void)key;
    box_mesh_gen_triangle_prism(mesh, true);
}

/** Key of an NGon.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_ngon(
    const void *value,
    box_shape_key_t *key)
{
    key->i[0] = box_segments(((const NGon*)value)->sides);
}

/** Scale of an NGon.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_ngon(
    const void *value,
    vec3 scale)
{
    float r = ((const NGon*)value)->radius;
    glm_vec3_copy((vec3){ r, r, 1.0f }, scale);
}

/** Generate an NGon mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_ngon(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_ngon(mesh, key->i[0]);
}

/** Key of a shape with the layout of Sphere.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_sphere(
    const void *value,
    box_shape_key_t *key)
{
    const Sphere *v = value;
    key->i[0] = box_segments(v->segments);
    key->i[1] = v->smooth;
}

/** Key of a level of detail of a smooth shape with the layout of Sphere.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_sphere(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Sphere v = *(const Sphere*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
    }

    box_key_sphere(&v, key);
}

/** Scale of a shape with the layout of Sphere.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_sphere(
    const void *value,
    vec3 scale)
{
    glm_vec3_broadcast(((const Sphere*)value)->radius, scale);
}

/** Generate a Sphere mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_sphere(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_sphere(mesh, key->i[0], key->i[1]);
}

/** Generate a HemiSphere mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_hemisphere(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_hemisphere(mesh, key->i[0], key->i[1]);
}

/** Key of an IcoSphere.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_icosphere(
    const void *value,
    box_shape_key_t *key)
{
    const IcoSphere *v = value;
    key->i[0] = v->segments < 0 ? 0 : (v->segments > 6 ? 6 : v->segments);
    key->i[1] = v->smooth;
}

/** Key of a level of detail of a smooth IcoSphere: one subdivision level less per level.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_icosphere(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    IcoSphere v = *(const IcoSphere*)value;
    if (v.smooth) {
        v.segments = v.segments < 0 ? 0 : (v.segments > 6 ? 6 : v.segments);
        v.segments = v.segments > level ? v.segments - level : 0;
    }

    box_key_icosphere(&v, key);
}

/** Generate an IcoSphere mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_icosphere(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_icosphere(mesh, key->i[0], key->i[1]);
}

/** Key of a shape with the layout of Cylinder.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_cylinder(
    const void *value,
    box_shape_key_t *key)
{
    const Cylinder *v = value;
    key->i[0] = box_segments(v->segments);
    key->i[1] = v->smooth;
}

/** Key of a level of detail of a smooth shape with the layout of Cylinder.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_cylinder(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Cylinder v = *(const Cylinder*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
    }

    box_key_cylinder(&v, key);
}

/** Scale of a shape with the layout of Cylinder.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_cylinder(
    const void *value,
    vec3 scale)
{
    const Cylinder *v = value;
    glm_vec3_copy((vec3){ v->radius, v->length, v->radius }, scale);
}

/** Generate a Cone mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_cone(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_frustum(mesh, key->i[0], key->i[1], 1.0f, 0.0f);
}

/** Generate a Cylinder mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_cylinder(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_frustum(mesh, key->i[0], key->i[1], 1.0f, 1.0f);
}

/** Key of a Torus.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_torus(
    const void *value,
    box_shape_key_t *key)
{
    const Torus *v = value;
    key->i[0] = box_segments(v->segments);
    key->i[1] = box_segments(v->rings);
    key->i[2] = v->smooth;
    key->f[0] = box_shape_quantize(box_ratio(v->tube, v->radius, 0.0f));
}

/** Key of a level of detail of a smooth Torus.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_torus(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Torus v = *(const Torus*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
        v.rings = box_shape_lod_count(box_segments(v.rings), 3, level);
    }

    box_key_torus(&v, key);
}

/** Scale of a Torus.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_torus(
    const void *value,
    vec3 scale)
{
    glm_vec3_broadcast(((const Torus*)value)->radius, scale);
}

/** Generate a Torus mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_torus(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_torus(mesh, key->i[0], key->i[1], key->i[2], key->f[0]);
}

/** Key of a Capsule.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_capsule(
    const void *value,
    box_shape_key_t *key)
{
    const Capsule *v = value;
    float length = box_ratio(v->length, v->radius, 0.0f);
    key->i[0] = box_segments(v->segments);
    key->i[1] = v->smooth;
    key->f[0] = box_shape_quantize(length > 0.0f ? length : 0.0f);
}

/** Key of a level of detail of a smooth Capsule.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_capsule(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Capsule v = *(const Capsule*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
    }

    box_key_capsule(&v, key);
}

/** Scale of a Capsule.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_capsule(
    const void *value,
    vec3 scale)
{
    glm_vec3_broadcast(((const Capsule*)value)->radius, scale);
}

/** Generate a Capsule mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_capsule(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_capsule(mesh, key->i[0], key->i[1], key->f[0]);
}

/** Key of a Tube.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_tube(
    const void *value,
    box_shape_key_t *key)
{
    const Tube *v = value;
    float wall = glm_clamp(box_ratio(v->thickness, v->radius, 1.0f), 0.0f, 1.0f);
    bool full = v->arc <= 0.0f || v->arc >= 2.0f * BOX_PI - 1e-4f;
    key->i[0] = box_segments(v->segments);
    key->i[1] = v->smooth;
    key->f[0] = box_shape_quantize(wall);
    key->f[1] = full ? 0.0f : box_shape_quantize(v->arc);
}

/** Key of a level of detail of a smooth Tube.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_tube(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Tube v = *(const Tube*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
    }

    box_key_tube(&v, key);
}

/** Generate a Tube mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_tube(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_tube(mesh, key->i[0], key->i[1], 1.0f - key->f[0], key->f[1]);
}

/** Key of a Frustum.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_frustum(
    const void *value,
    box_shape_key_t *key)
{
    const Frustum *v = value;
    float max = glm_max(v->radius_bottom, v->radius_top);
    key->i[0] = box_segments(v->segments);
    key->i[1] = v->smooth;
    key->f[0] = box_shape_quantize(glm_max(box_ratio(v->radius_bottom, max, 1.0f), 0.0f));
    key->f[1] = box_shape_quantize(glm_max(box_ratio(v->radius_top, max, 1.0f), 0.0f));
}

/** Key of a level of detail of a smooth Frustum.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_frustum(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    Frustum v = *(const Frustum*)value;
    if (v.smooth) {
        v.segments = box_shape_lod_count(box_segments(v.segments), 3, level);
    }

    box_key_frustum(&v, key);
}

/** Scale of a Frustum.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_frustum(
    const void *value,
    vec3 scale)
{
    const Frustum *v = value;
    float max = glm_max(glm_max(v->radius_bottom, v->radius_top), 0.0f);
    glm_vec3_copy((vec3){ max, v->length, max }, scale);
}

/** Generate a Frustum mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_frustum(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_frustum(mesh, key->i[0], key->i[1], key->f[0], key->f[1]);
}

/** Key of a RoundedBox.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_rounded_box(
    const void *value,
    box_shape_key_t *key)
{
    const RoundedBox *v = value;
    float max = glm_max(glm_max(v->x, v->y), v->z);
    float smallest = 1.0f;
    vec3 size = { v->x, v->y, v->z };
    for (int32_t i = 0; i < 3; i ++) {
        float f = box_shape_quantize(box_ratio(size[i], max, 1.0f));
        key->f[i] = glm_max(f, 1.0f / 4096.0f);
        smallest = glm_min(smallest, key->f[i]);
    }

    float radius = glm_clamp(box_ratio(v->radius, max, 0.0f), 0.0f, 0.5f * smallest);
    key->f[3] = glm_min(box_shape_quantize(radius), 0.5f * smallest);
    key->i[0] = v->segments < 1 ? 1 : (v->segments > 64 ? 64 : v->segments);
}

/** Key of a level of detail of a RoundedBox.
 *
 * @param value The shape value
 * @param level The level
 * @param key The key
 */
static void box_lod_rounded_box(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    RoundedBox v = *(const RoundedBox*)value;
    v.segments = box_shape_lod_even(v.segments < 1 ? 1 : (v.segments > 64 ? 64 : v.segments),
        level);
    box_key_rounded_box(&v, key);
}

/** Scale of a RoundedBox.
 *
 * @param value The shape value
 * @param scale The scale
 */
static void box_scale_rounded_box(
    const void *value,
    vec3 scale)
{
    const RoundedBox *v = value;
    glm_vec3_broadcast(glm_max(glm_max(glm_max(v->x, v->y), v->z), 0.0f), scale);
}

/** Generate a RoundedBox mesh.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_rounded_box(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_rounded_box(mesh, key->f, key->f[3], key->i[0]);
}

/** Marker in i[1] of the key of a Bevel whose edges share one radius */
#define BOX_BEVEL_UNIFORM (1)

/** Marker in i[1] of the key of a Bevel with a radius per edge */
#define BOX_BEVEL_PER_EDGE (2)

/** Get a radius that applies, with negative and non-finite radii as 0.
 *
 * @param radius The authored radius
 * @return The radius, at least 0
 */
static float box_bevel_radius(
    float radius)
{
    return isfinite(radius) && radius > 0.0f ? radius : 0.0f;
}

/** Get the edge radii of a Bevel in the order of its members, with top and bottom for the top
 * and bottom edges whose own radius is 0.
 *
 * @param bevel The bevel
 * @param radii The radii to fill, negative and non-finite radii as 0
 */
static void box_bevel_edges(
    const Bevel *bevel,
    float radii[BOX_BEVEL_EDGES])
{
    const float edges[BOX_BEVEL_EDGES] = {
        bevel->top_front, bevel->top_back, bevel->top_left, bevel->top_right,
        bevel->bottom_front, bevel->bottom_back, bevel->bottom_left, bevel->bottom_right,
        bevel->front_left, bevel->front_right, bevel->back_left, bevel->back_right
    };
    float top = box_bevel_radius(bevel->top), bottom = box_bevel_radius(bevel->bottom);
    for (int32_t i = 0; i < BOX_BEVEL_EDGES; i ++) {
        radii[i] = box_bevel_radius(edges[i]);
        if (radii[i] == 0.0f && i < 4) {
            radii[i] = top;
        } else if (radii[i] == 0.0f && i < 8) {
            radii[i] = bottom;
        }
    }
}

/** Check whether every edge of a Bevel has the same radius.
 *
 * @param bevel The bevel
 * @param radius Receives the radius of every edge
 * @return True when radius applies or the 12 edge radii are equal
 */
static bool box_bevel_uniform(
    const Bevel *bevel,
    float *radius)
{
    bool result = true;
    if (bevel->radius != 0.0f) {
        *radius = box_bevel_radius(bevel->radius);
    } else {
        float radii[BOX_BEVEL_EDGES];
        box_bevel_edges(bevel, radii);
        *radius = radii[0];
        for (int32_t i = 1; i < BOX_BEVEL_EDGES; i ++) {
            result &= radii[i] == radii[0];
        }
    }

    return result;
}

bool box_bevel_flat(
    const Bevel *bevel)
{
    float radius = 0.0f;

    return box_bevel_uniform(bevel, &radius) && radius == 0.0f;
}

/** Get the quantized proportions and edge radii of a Bevel relative to the largest size.
 *
 * @param value The Box and the Bevel
 * @param size The proportions to fill, as in the key of a RoundedBox
 * @param radii The edge radii to fill
 */
static void box_bevel_quantize(
    const box_bevel_value_t *value,
    vec3 size,
    float radii[BOX_BEVEL_EDGES])
{
    const Box *box = &value->box;
    float max = glm_max(glm_max(box->x, box->y), box->z);
    vec3 sizes = { box->x, box->y, box->z };
    for (int32_t i = 0; i < 3; i ++) {
        float f = box_shape_quantize(box_ratio(sizes[i], max, 1.0f));
        size[i] = glm_max(f, 1.0f / 4096.0f);
    }

    box_bevel_edges(&value->bevel, radii);
    for (int32_t i = 0; i < BOX_BEVEL_EDGES; i ++) {
        radii[i] = glm_max(box_shape_quantize(box_ratio(radii[i], max, 0.0f)), 0.0f);
    }
}

int32_t box_bevel_segments(
    const Bevel *bevel)
{
    return bevel->segments < 1 ? 1 : (bevel->segments > 64 ? 64 : bevel->segments);
}

float box_bevel_rim(
    const Bevel *bevel,
    float rim)
{
    return box_bevel_radius(bevel->radius != 0.0f ? bevel->radius : rim);
}

/** Key of a Bevel: the key of a RoundedBox when the edges share a radius, else a hash of the
 * quantized proportions and edge radii.
 *
 * @param value The Box and the Bevel
 * @param key The key
 */
static void box_key_bevel(
    const void *value,
    box_shape_key_t *key)
{
    const box_bevel_value_t *v = value;
    float radius;
    if (box_bevel_uniform(&v->bevel, &radius)) {
        RoundedBox rounded = {
            .x = v->box.x, .y = v->box.y, .z = v->box.z,
            .radius = radius, .segments = v->bevel.segments
        };
        box_key_rounded_box(&rounded, key);
        key->i[1] = BOX_BEVEL_UNIFORM;
    } else {
        vec3 size;
        float radii[BOX_BEVEL_EDGES];
        box_bevel_quantize(v, size, radii);
        uint64_t hash = 14695981039346656037ull;
        const uint8_t *bytes = (const uint8_t*)size;
        for (size_t i = 0; i < sizeof(vec3); i ++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }

        bytes = (const uint8_t*)radii;
        for (size_t i = 0; i < sizeof(radii); i ++) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }

        glm_vec3_copy(size, key->f);
        key->i[0] = box_bevel_segments(&v->bevel);
        key->i[1] = BOX_BEVEL_PER_EDGE;
        key->i[2] = (int32_t)(uint32_t)hash;
        key->i[3] = (int32_t)(uint32_t)(hash >> 32);
    }
}

/** Key of a level of detail of a Bevel of a Box.
 *
 * @param value The Box and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_value_t v = *(const box_bevel_value_t*)value;
    v.bevel.segments = box_shape_lod_even(box_bevel_segments(&v.bevel), level);
    box_key_bevel(&v, key);
}

/** Scale of a Bevel, the largest size of its Box as for a RoundedBox.
 *
 * @param value The Box and the Bevel
 * @param scale The scale
 */
static void box_scale_bevel(
    const void *value,
    vec3 scale)
{
    const Box *v = &((const box_bevel_value_t*)value)->box;
    glm_vec3_broadcast(glm_max(glm_max(glm_max(v->x, v->y), v->z), 0.0f), scale);
}

/** Generate a Bevel mesh.
 *
 * @param value The Box and the Bevel that produced the key
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    if (key->i[1] == BOX_BEVEL_UNIFORM) {
        box_mesh_gen_rounded_box(mesh, key->f, key->f[3], key->i[0]);
    } else {
        vec3 size;
        float radii[BOX_BEVEL_EDGES];
        box_bevel_quantize(value, size, radii);
        box_mesh_gen_bevel(mesh, size, radii, key->i[0]);
    }
}

/** Check whether the Bevel of a Box rounds no edge.
 *
 * @param value The Box and the Bevel
 * @return True when the Box resolves as a plain Box
 */
static bool box_bevel_box_flat(
    const void *value)
{
    return box_bevel_flat(&((const box_bevel_value_t*)value)->bevel);
}

/** Marker in i[1] of the key of a beveled round shape */
#define BOX_BEVEL_ROUND (3)

/** Marker in i[1] of the key of a beveled prism */
#define BOX_BEVEL_PRISM (4)

/** Hash floats into a running FNV-1a hash.
 *
 * @param hash The running hash
 * @param values The floats
 * @param count Number of floats
 * @return The new hash
 */
static uint64_t box_hash_floats(
    uint64_t hash,
    const float *values,
    int32_t count)
{
    const uint8_t *bytes = (const uint8_t*)values;
    for (size_t i = 0; i < (size_t)count * sizeof(float); i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
}

/** Store a hash in i[2] and i[3] of a key.
 *
 * @param hash The hash
 * @param key The key
 */
static void box_key_hash(
    uint64_t hash,
    box_shape_key_t *key)
{
    key->i[2] = (int32_t)(uint32_t)hash;
    key->i[3] = (int32_t)(uint32_t)(hash >> 32);
}

/** Fill the rims of a beveled round shape from its Bevel and fit them to its cross section.
 *
 * Radii are quantized relative to the scale radius before they are fitted, the same way for
 * the key and the generator.
 *
 * @param shape The shape, with its cross section filled in
 * @param bevel The bevel
 * @param radius The scale radius
 */
static void box_bevel_round_rims(
    box_bevel_round_t *shape,
    const Bevel *bevel,
    float radius)
{
    float bottom = box_shape_quantize(box_ratio(box_bevel_rim(bevel, bevel->bottom), radius, 0));
    float top = box_shape_quantize(box_ratio(box_bevel_rim(bevel, bevel->top), radius, 0));
    shape->rims[BoxRimBottomOuter] = shape->rims[BoxRimBottomInner] = glm_max(bottom, 0.0f);
    shape->rims[BoxRimTopOuter] = shape->rims[BoxRimTopInner] = glm_max(top, 0.0f);
    shape->steps = box_bevel_segments(bevel);
    if (!(radius > 0.0f) || !(shape->length > 0.0f)) {
        for (int32_t r = 0; r < BoxRimCount; r ++) {
            shape->rims[r] = 0.0f;
        }
    } else {
        box_bevel_round_fit(shape);
    }
}

/** Get the beveled round shape of a Cylinder and its Bevel.
 *
 * @param value The Cylinder and the Bevel
 * @param cone True for a Cone, which shares the layout of Cylinder
 * @param out The shape
 */
static void box_bevel_cylinder(
    const box_bevel_cylinder_t *value,
    bool cone,
    box_bevel_round_t *out)
{
    const Cylinder *v = &value->host;
    *out = (box_bevel_round_t){
        .segments = box_segments(v->segments),
        .smooth = v->smooth,
        .length = box_shape_quantize(box_ratio(v->length, v->radius, 0.0f)),
        .bottom = 1.0f,
        .top = cone ? 0.0f : 1.0f
    };
    box_bevel_round_rims(out, &value->bevel, v->radius);
}

/** Get the beveled round shape of a Frustum and its Bevel.
 *
 * @param value The Frustum and the Bevel
 * @param out The shape
 */
static void box_bevel_frustum(
    const box_bevel_frustum_t *value,
    box_bevel_round_t *out)
{
    const Frustum *v = &value->host;
    float max = glm_max(v->radius_bottom, v->radius_top);
    *out = (box_bevel_round_t){
        .segments = box_segments(v->segments),
        .smooth = v->smooth,
        .length = box_shape_quantize(box_ratio(v->length, max, 0.0f)),
        .bottom = box_shape_quantize(glm_max(box_ratio(v->radius_bottom, max, 1.0f), 0.0f)),
        .top = box_shape_quantize(glm_max(box_ratio(v->radius_top, max, 1.0f), 0.0f))
    };
    box_bevel_round_rims(out, &value->bevel, max);
}

/** Get the beveled round shape of a Tube and its Bevel.
 *
 * @param value The Tube and the Bevel
 * @param out The shape
 */
static void box_bevel_tube(
    const box_bevel_tube_t *value,
    box_bevel_round_t *out)
{
    const Tube *v = &value->host;
    float wall = glm_clamp(box_ratio(v->thickness, v->radius, 1.0f), 0.0f, 1.0f);
    bool full = v->arc <= 0.0f || v->arc >= 2.0f * BOX_PI - 1e-4f;
    *out = (box_bevel_round_t){
        .segments = box_segments(v->segments),
        .smooth = v->smooth,
        .length = box_shape_quantize(box_ratio(v->length, v->radius, 0.0f)),
        .bottom = 1.0f,
        .top = 1.0f,
        .inner = glm_max(1.0f - box_shape_quantize(wall), 0.0f),
        .arc = full ? 0.0f : box_shape_quantize(v->arc)
    };
    box_bevel_round_rims(out, &value->bevel, v->radius);
}

/** Check whether a beveled round shape rounds no rim.
 *
 * @param shape The shape
 * @return True when every fitted rim radius is 0
 */
static bool box_bevel_round_flat(
    const box_bevel_round_t *shape)
{
    bool result = true;
    for (int32_t r = 0; r < BoxRimCount; r ++) {
        result &= shape->rims[r] == 0.0f;
    }

    return result;
}

/** Key of a beveled round shape: a hash of its cross section, its rim radii relative to the
 * scale radius and to the length, the steps per rim and smooth, with the segments in i[0] and
 * the steps per rim in i[1] above the marker, which the generators read.
 *
 * @param shape The shape
 * @param key The key
 */
static void box_key_bevel_round(
    const box_bevel_round_t *shape,
    box_shape_key_t *key)
{
    float values[5 + 2 * BoxRimCount + 2] = {
        shape->length, shape->bottom, shape->top, shape->inner, shape->arc
    };
    for (int32_t r = 0; r < BoxRimCount; r ++) {
        values[5 + 2 * r] = shape->rims[r];
        values[6 + 2 * r] = box_shape_quantize(box_ratio(shape->rims[r], shape->length, 0.0f));
    }

    values[5 + 2 * BoxRimCount] = (float)shape->steps;
    values[6 + 2 * BoxRimCount] = shape->smooth ? 1.0f : 0.0f;
    key->i[0] = shape->segments;
    key->i[1] = BOX_BEVEL_ROUND | (shape->steps << 8);
    box_key_hash(box_hash_floats(14695981039346656037ull, values,
        (int32_t)(sizeof(values) / sizeof(values[0]))), key);
}

/** Reduce the counts of a beveled round shape to a level of detail: the segments around the
 * axis of a smooth shape and the steps per rim, at least 2.
 *
 * @param segments The segments around the axis of the host
 * @param smooth Whether the host is smooth
 * @param bevel The Bevel
 * @param level The level
 */
static void box_bevel_round_lod(
    int32_t *segments,
    bool smooth,
    Bevel *bevel,
    int32_t level)
{
    if (smooth) {
        *segments = box_shape_lod_count(box_segments(*segments), 3, level);
    }

    bevel->segments = box_shape_lod_count(box_bevel_segments(bevel), 2, level);
}

/** Set the counts of a beveled round shape to those of its key.
 *
 * @param key The key
 * @param segments The segments around the axis of the host
 * @param bevel The Bevel, whose steps per rim are set
 */
static void box_bevel_round_counts(
    const box_shape_key_t *key,
    int32_t *segments,
    Bevel *bevel)
{
    *segments = key->i[0];
    bevel->segments = key->i[1] >> 8;
}

/** Key of a beveled Cylinder.
 *
 * @param value The Cylinder and the Bevel
 * @param key The key
 */
static void box_key_bevel_cylinder(
    const void *value,
    box_shape_key_t *key)
{
    box_bevel_round_t shape;
    box_bevel_cylinder(value, false, &shape);
    box_key_bevel_round(&shape, key);
}

/** Key of a beveled Cone.
 *
 * @param value The Cone and the Bevel
 * @param key The key
 */
static void box_key_bevel_cone(
    const void *value,
    box_shape_key_t *key)
{
    box_bevel_round_t shape;
    box_bevel_cylinder(value, true, &shape);
    box_key_bevel_round(&shape, key);
}

/** Key of a beveled Frustum.
 *
 * @param value The Frustum and the Bevel
 * @param key The key
 */
static void box_key_bevel_frustum(
    const void *value,
    box_shape_key_t *key)
{
    box_bevel_round_t shape;
    box_bevel_frustum(value, &shape);
    box_key_bevel_round(&shape, key);
}

/** Key of a beveled Tube.
 *
 * @param value The Tube and the Bevel
 * @param key The key
 */
static void box_key_bevel_tube(
    const void *value,
    box_shape_key_t *key)
{
    box_bevel_round_t shape;
    box_bevel_tube(value, &shape);
    box_key_bevel_round(&shape, key);
}

/** Key of a level of detail of a beveled Cylinder.
 *
 * @param value The Cylinder and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel_cylinder(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_cylinder_t v = *(const box_bevel_cylinder_t*)value;
    box_bevel_round_lod(&v.host.segments, v.host.smooth, &v.bevel, level);
    box_key_bevel_cylinder(&v, key);
}

/** Generate a beveled Cylinder mesh.
 *
 * @param value The Cylinder and the Bevel that produced the key
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_cylinder(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    box_bevel_round_t shape;
    box_bevel_cylinder_t v = *(const box_bevel_cylinder_t*)value;
    box_bevel_round_counts(key, &v.host.segments, &v.bevel);
    box_bevel_cylinder(&v, false, &shape);
    box_mesh_gen_bevel_round(mesh, &shape);
}

/** Key of a level of detail of a beveled Cone.
 *
 * @param value The Cone and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel_cone(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_cylinder_t v = *(const box_bevel_cylinder_t*)value;
    box_bevel_round_lod(&v.host.segments, v.host.smooth, &v.bevel, level);
    box_key_bevel_cone(&v, key);
}

/** Generate a beveled Cone mesh.
 *
 * @param value The Cone and the Bevel that produced the key
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_cone(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    box_bevel_round_t shape;
    box_bevel_cylinder_t v = *(const box_bevel_cylinder_t*)value;
    box_bevel_round_counts(key, &v.host.segments, &v.bevel);
    box_bevel_cylinder(&v, true, &shape);
    box_mesh_gen_bevel_round(mesh, &shape);
}

/** Key of a level of detail of a beveled Frustum.
 *
 * @param value The Frustum and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel_frustum(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_frustum_t v = *(const box_bevel_frustum_t*)value;
    box_bevel_round_lod(&v.host.segments, v.host.smooth, &v.bevel, level);
    box_key_bevel_frustum(&v, key);
}

/** Generate a beveled Frustum mesh.
 *
 * @param value The Frustum and the Bevel that produced the key
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_frustum(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    box_bevel_round_t shape;
    box_bevel_frustum_t v = *(const box_bevel_frustum_t*)value;
    box_bevel_round_counts(key, &v.host.segments, &v.bevel);
    box_bevel_frustum(&v, &shape);
    box_mesh_gen_bevel_round(mesh, &shape);
}

/** Key of a level of detail of a beveled Tube.
 *
 * @param value The Tube and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel_tube(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_tube_t v = *(const box_bevel_tube_t*)value;
    box_bevel_round_lod(&v.host.segments, v.host.smooth, &v.bevel, level);
    box_key_bevel_tube(&v, key);
}

/** Generate a beveled Tube mesh.
 *
 * @param value The Tube and the Bevel that produced the key
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_tube(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    box_bevel_round_t shape;
    box_bevel_tube_t v = *(const box_bevel_tube_t*)value;
    box_bevel_round_counts(key, &v.host.segments, &v.bevel);
    box_bevel_tube(&v, &shape);
    box_mesh_gen_bevel_round(mesh, &shape);
}

/** Check whether the Bevel of a Cylinder rounds no rim.
 *
 * @param value The Cylinder and the Bevel
 * @return True when the Cylinder resolves as a plain Cylinder
 */
static bool box_bevel_cylinder_flat(
    const void *value)
{
    box_bevel_round_t shape;
    box_bevel_cylinder(value, false, &shape);

    return box_bevel_round_flat(&shape);
}

/** Check whether the Bevel of a Cone rounds its base rim, the only rim it has.
 *
 * @param value The Cone and the Bevel
 * @return True when the Cone resolves as a plain Cone
 */
static bool box_bevel_cone_flat(
    const void *value)
{
    box_bevel_round_t shape;
    box_bevel_cylinder(value, true, &shape);

    return box_bevel_round_flat(&shape);
}

/** Check whether the Bevel of a Frustum rounds no rim; a rim of radius 0 is no edge.
 *
 * @param value The Frustum and the Bevel
 * @return True when the Frustum resolves as a plain Frustum
 */
static bool box_bevel_frustum_flat(
    const void *value)
{
    box_bevel_round_t shape;
    box_bevel_frustum(value, &shape);

    return box_bevel_round_flat(&shape);
}

/** Check whether the Bevel of a Tube rounds no rim.
 *
 * @param value The Tube and the Bevel
 * @return True when the Tube resolves as a plain Tube
 */
static bool box_bevel_tube_flat(
    const void *value)
{
    box_bevel_round_t shape;
    box_bevel_tube(value, &shape);

    return box_bevel_round_flat(&shape);
}

/** Key of a beveled prism: the quantized proportions and radius relative to the largest size,
 * hashed, with a marker.
 *
 * @param value The prism and the Bevel
 * @param key The key
 */
static void box_key_bevel_prism(
    const void *value,
    box_shape_key_t *key)
{
    const box_bevel_prism_t *v = value;
    const TrianglePrism *prism = &v->host;
    float max = glm_max(glm_max(prism->x, prism->y), prism->z);
    vec3 sizes = { prism->x, prism->y, prism->z };
    for (int32_t i = 0; i < 3; i ++) {
        float f = box_shape_quantize(box_ratio(sizes[i], max, 1.0f));
        key->f[i] = glm_max(f, 1.0f / 4096.0f);
    }

    key->f[3] = glm_max(box_shape_quantize(box_ratio(box_bevel_radius(v->bevel.radius),
        max, 0.0f)), 0.0f);
    key->i[0] = box_bevel_segments(&v->bevel);
    key->i[1] = BOX_BEVEL_PRISM;
    box_key_hash(box_hash_floats(14695981039346656037ull, key->f, 4), key);
}

/** Key of a level of detail of a beveled TrianglePrism or RightTrianglePrism.
 *
 * @param value The prism and the Bevel
 * @param level The level
 * @param key The key
 */
static void box_lod_bevel_prism(
    const void *value,
    int32_t level,
    box_shape_key_t *key)
{
    box_bevel_prism_t v = *(const box_bevel_prism_t*)value;
    v.bevel.segments = box_shape_lod_even(box_bevel_segments(&v.bevel), level);
    box_key_bevel_prism(&v, key);
}

/** Key of a beveled RightTrianglePrism, the key of a TrianglePrism.
 *
 * @param value The prism and the Bevel
 * @param key The key
 */
static void box_key_bevel_right_prism(
    const void *value,
    box_shape_key_t *key)
{
    box_key_bevel_prism(value, key);
}

/** Generate a beveled TrianglePrism mesh.
 *
 * @param value The prism and the Bevel
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_prism(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_bevel_prism(mesh, false, key->f, key->f[3], key->i[0]);
}

/** Generate a beveled RightTrianglePrism mesh.
 *
 * @param value The prism and the Bevel
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_bevel_right_prism(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    box_mesh_gen_bevel_prism(mesh, true, key->f, key->f[3], key->i[0]);
}

/** Check whether the Bevel of a prism rounds nothing: only radius applies.
 *
 * @param value The prism and the Bevel
 * @return True when the prism resolves as a plain prism
 */
static bool box_bevel_prism_flat(
    const void *value)
{
    const box_bevel_prism_t *v = value;
    box_shape_key_t key = {0};
    box_key_bevel_prism(value, &key);

    return key.f[3] == 0.0f || !(v->host.x > 0.0f) || !(v->host.y > 0.0f) ||
        !(v->host.z > 0.0f);
}

/** Hash bytes into a running FNV-1a hash.
 *
 * @param hash The running hash
 * @param vec The vector to hash
 * @param size Size of one element
 * @return The new hash
 */
static uint64_t box_hash_vec(
    uint64_t hash,
    const ecs_vec_t *vec,
    ecs_size_t size)
{
    int32_t count = ecs_vec_count(vec);
    const uint8_t *bytes = ecs_vec_first(vec);
    hash = (hash ^ (uint64_t)(uint32_t)count) * 1099511628211ull;
    for (ecs_size_t i = 0; i < count * size; i ++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
}

/** Key of a Mesh3, a hash of its contents.
 *
 * @param value The shape value
 * @param key The key
 */
static void box_key_mesh3(
    const void *value,
    box_shape_key_t *key)
{
    const Mesh3 *v = value;
    uint64_t hash = 14695981039346656037ull;
    hash = box_hash_vec(hash, &v->vertices, ECS_SIZEOF(vec3));
    hash = box_hash_vec(hash, &v->normals, ECS_SIZEOF(vec3));
    hash = box_hash_vec(hash, &v->uvs, ECS_SIZEOF(vec2));
    hash = box_hash_vec(hash, &v->indices, ECS_SIZEOF(uint32_t));
    hash = box_hash_vec(hash, &v->colors, ECS_SIZEOF(rgba));
    hash = box_hash_vec(hash, &v->data0, ECS_SIZEOF(vec4));
    hash = box_hash_vec(hash, &v->data1, ECS_SIZEOF(vec4));
    hash = box_hash_vec(hash, &v->data2, ECS_SIZEOF(vec4));
    hash = box_hash_vec(hash, &v->data3, ECS_SIZEOF(vec4));
    key->i[0] = (int32_t)(uint32_t)hash;
    key->i[1] = (int32_t)(uint32_t)(hash >> 32);
    key->i[2] = ecs_vec_count(&v->vertices);
    key->i[3] = ecs_vec_count(&v->indices);
}

/** Generate the mesh of a Mesh3.
 *
 * @param value The shape value
 * @param key The key
 * @param mesh The mesh
 */
static void box_gen_mesh3(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)key;
    box_mesh_gen_mesh3(mesh, value);
}

/** Register one built-in shape.
 *
 * @param world The world
 * @param component The shape component
 * @param key The key function
 * @param generate The generator
 * @param scale The scale function
 * @param exclude Component of entities another resolver owns, Bevel for a host, 0 for none
 * @param lod The key function of the levels of detail, NULL for none
 */
static void box_shape_builtin(
    ecs_world_t *world,
    ecs_entity_t component,
    box_shape_key_fn key,
    box_shape_generate_fn generate,
    box_shape_scale_fn scale,
    ecs_entity_t exclude,
    box_shape_lod_fn lod)
{
    box_shape_register(world, &(box_shape_desc_t){
        .component = component,
        .key = key,
        .generate = generate,
        .scale = scale,
        .exclude = exclude,
        .lod = lod
    });
}

void box_geometry_shapes_init(
    ecs_world_t *world)
{
    ecs_entity_t bevel = ecs_id(Bevel);
    box_shape_builtin(world, ecs_id(Mesh3), box_key_mesh3, box_gen_mesh3, NULL, 0, NULL);
    box_shape_builtin(world, ecs_id(Box), box_key_none, box_gen_box, box_scale_xyz, bevel, NULL);
    box_shape_builtin(world, ecs_id(Quad), box_key_none, box_gen_quad, box_scale_xy, 0, NULL);
    box_shape_builtin(world, ecs_id(Triangle), box_key_none, box_gen_triangle, box_scale_xy, 0,
        NULL);
    box_shape_builtin(world, ecs_id(RightTriangle), box_key_none,
        box_gen_right_triangle, box_scale_xy, 0, NULL);
    box_shape_builtin(world, ecs_id(TrianglePrism), box_key_none,
        box_gen_triangle_prism, box_scale_xyz, bevel, NULL);
    box_shape_builtin(world, ecs_id(RightTrianglePrism), box_key_none,
        box_gen_right_triangle_prism, box_scale_xyz, bevel, NULL);
    box_shape_builtin(world, ecs_id(NGon), box_key_ngon, box_gen_ngon, box_scale_ngon, 0, NULL);
    box_shape_builtin(world, ecs_id(Sphere), box_key_sphere, box_gen_sphere,
        box_scale_sphere, 0, box_lod_sphere);
    box_shape_builtin(world, ecs_id(HemiSphere), box_key_sphere,
        box_gen_hemisphere, box_scale_sphere, 0, box_lod_sphere);
    box_shape_builtin(world, ecs_id(IcoSphere), box_key_icosphere,
        box_gen_icosphere, box_scale_sphere, 0, box_lod_icosphere);
    box_shape_builtin(world, ecs_id(Cone), box_key_cylinder, box_gen_cone,
        box_scale_cylinder, bevel, box_lod_cylinder);
    box_shape_builtin(world, ecs_id(Cylinder), box_key_cylinder,
        box_gen_cylinder, box_scale_cylinder, bevel, box_lod_cylinder);
    box_shape_builtin(world, ecs_id(Torus), box_key_torus, box_gen_torus, box_scale_torus, 0,
        box_lod_torus);
    box_shape_builtin(world, ecs_id(Capsule), box_key_capsule,
        box_gen_capsule, box_scale_capsule, 0, box_lod_capsule);
    box_shape_builtin(world, ecs_id(Tube), box_key_tube, box_gen_tube,
        box_scale_cylinder, bevel, box_lod_tube);
    box_shape_builtin(world, ecs_id(Frustum), box_key_frustum,
        box_gen_frustum, box_scale_frustum, bevel, box_lod_frustum);
    box_shape_builtin(world, ecs_id(RoundedBox), box_key_rounded_box,
        box_gen_rounded_box, box_scale_rounded_box, 0, box_lod_rounded_box);

    box_shape_count_hooks(world, ecs_id(NGon), BOX_NGON_SIDES);
    box_shape_count_hooks(world, ecs_id(Sphere), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(HemiSphere), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(Cone), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(Cylinder), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(Capsule), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(Tube), BOX_SEGMENTS);
    box_shape_count_hooks(world, ecs_id(Frustum), BOX_SEGMENTS);
    ecs_set_hooks(world, Torus, { .ctor = box_torus_ctor });
    ecs_set_hooks(world, RoundedBox, { .ctor = box_rounded_box_ctor });
    ecs_set_hooks(world, Bevel, { .ctor = box_bevel_ctor });

    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel, .generate = box_gen_bevel, .scale = box_scale_bevel,
        .lod = box_lod_bevel },
        ecs_id(Box), ECS_SIZEOF(Box), (ecs_size_t)offsetof(box_bevel_value_t, bevel),
        ECS_SIZEOF(box_bevel_value_t), box_bevel_box_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_cylinder, .generate = box_gen_bevel_cylinder,
        .scale = box_scale_cylinder, .lod = box_lod_bevel_cylinder },
        ecs_id(Cylinder), ECS_SIZEOF(Cylinder), (ecs_size_t)offsetof(box_bevel_cylinder_t, bevel),
        ECS_SIZEOF(box_bevel_cylinder_t), box_bevel_cylinder_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_cone, .generate = box_gen_bevel_cone,
        .scale = box_scale_cylinder, .lod = box_lod_bevel_cone },
        ecs_id(Cone), ECS_SIZEOF(Cone), (ecs_size_t)offsetof(box_bevel_cylinder_t, bevel),
        ECS_SIZEOF(box_bevel_cylinder_t), box_bevel_cone_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_frustum, .generate = box_gen_bevel_frustum,
        .scale = box_scale_frustum, .lod = box_lod_bevel_frustum },
        ecs_id(Frustum), ECS_SIZEOF(Frustum), (ecs_size_t)offsetof(box_bevel_frustum_t, bevel),
        ECS_SIZEOF(box_bevel_frustum_t), box_bevel_frustum_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_tube, .generate = box_gen_bevel_tube,
        .scale = box_scale_cylinder, .lod = box_lod_bevel_tube },
        ecs_id(Tube), ECS_SIZEOF(Tube), (ecs_size_t)offsetof(box_bevel_tube_t, bevel),
        ECS_SIZEOF(box_bevel_tube_t), box_bevel_tube_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_prism, .generate = box_gen_bevel_prism,
        .scale = box_scale_xyz, .lod = box_lod_bevel_prism },
        ecs_id(TrianglePrism), ECS_SIZEOF(TrianglePrism),
        (ecs_size_t)offsetof(box_bevel_prism_t, bevel), ECS_SIZEOF(box_bevel_prism_t),
        box_bevel_prism_flat);
    box_geometry_bevel_host(world, &(box_shape_desc_t){
        .key = box_key_bevel_right_prism, .generate = box_gen_bevel_right_prism,
        .scale = box_scale_xyz, .lod = box_lod_bevel_prism },
        ecs_id(RightTrianglePrism), ECS_SIZEOF(RightTrianglePrism),
        (ecs_size_t)offsetof(box_bevel_prism_t, bevel), ECS_SIZEOF(box_bevel_prism_t),
        box_bevel_prism_flat);
}
