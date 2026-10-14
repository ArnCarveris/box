/**
 * @file geometry/shapes.c
 * @brief Key, generator and scale functions of the built-in 3D shapes.
 */

#include "mesh_cache.h"
#include "mesh_gen.h"

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
 */
static void box_shape_builtin(
    ecs_world_t *world,
    ecs_entity_t component,
    box_shape_key_fn key,
    box_shape_generate_fn generate,
    box_shape_scale_fn scale)
{
    box_shape_register(world, &(box_shape_desc_t){
        .component = component,
        .key = key,
        .generate = generate,
        .scale = scale
    });
}

void box_geometry_shapes_init(
    ecs_world_t *world)
{
    box_shape_builtin(world, ecs_id(Mesh3), box_key_mesh3, box_gen_mesh3, NULL);
    box_shape_builtin(world, ecs_id(Box), box_key_none, box_gen_box, box_scale_xyz);
    box_shape_builtin(world, ecs_id(Quad), box_key_none, box_gen_quad, box_scale_xy);
    box_shape_builtin(world, ecs_id(Triangle), box_key_none, box_gen_triangle, box_scale_xy);
    box_shape_builtin(world, ecs_id(RightTriangle), box_key_none,
        box_gen_right_triangle, box_scale_xy);
    box_shape_builtin(world, ecs_id(TrianglePrism), box_key_none,
        box_gen_triangle_prism, box_scale_xyz);
    box_shape_builtin(world, ecs_id(RightTrianglePrism), box_key_none,
        box_gen_right_triangle_prism, box_scale_xyz);
    box_shape_builtin(world, ecs_id(NGon), box_key_ngon, box_gen_ngon, box_scale_ngon);
    box_shape_builtin(world, ecs_id(Sphere), box_key_sphere, box_gen_sphere, box_scale_sphere);
    box_shape_builtin(world, ecs_id(HemiSphere), box_key_sphere,
        box_gen_hemisphere, box_scale_sphere);
    box_shape_builtin(world, ecs_id(IcoSphere), box_key_icosphere,
        box_gen_icosphere, box_scale_sphere);
    box_shape_builtin(world, ecs_id(Cone), box_key_cylinder, box_gen_cone, box_scale_cylinder);
    box_shape_builtin(world, ecs_id(Cylinder), box_key_cylinder,
        box_gen_cylinder, box_scale_cylinder);
    box_shape_builtin(world, ecs_id(Torus), box_key_torus, box_gen_torus, box_scale_torus);
    box_shape_builtin(world, ecs_id(Capsule), box_key_capsule,
        box_gen_capsule, box_scale_capsule);
    box_shape_builtin(world, ecs_id(Tube), box_key_tube, box_gen_tube, box_scale_cylinder);
    box_shape_builtin(world, ecs_id(Frustum), box_key_frustum,
        box_gen_frustum, box_scale_frustum);
    box_shape_builtin(world, ecs_id(RoundedBox), box_key_rounded_box,
        box_gen_rounded_box, box_scale_rounded_box);

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
}
