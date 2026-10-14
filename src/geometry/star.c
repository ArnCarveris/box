/**
 * @file geometry/star.c
 * @brief The Star shape, registered with the public shape API as a project would.
 */

#include "mesh_cache.h"
#include <math.h>

/** Key of a Star: the number of points and the inner radius.
 *
 * @param value The Star
 * @param key The key
 */
static void box_star_key(
    const void *value,
    box_shape_key_t *key)
{
    const Star *star = value;
    key->i[0] = star->points < 3 ? 3 : (star->points > 256 ? 256 : star->points);
    key->f[0] = box_shape_quantize(glm_clamp(star->inner, 0.0f, 1.0f));
}

/** Generate a star with unit outer radius in the XY plane facing +Z.
 *
 * @param value The Star
 * @param key The key
 * @param mesh The mesh
 */
static void box_star_generate(
    const void *value,
    const box_shape_key_t *key,
    box_mesh_t *mesh)
{
    (void)value;
    int32_t rim = key->i[0] * 2;
    vec3 normal = { 0.0f, 0.0f, 1.0f };
    int32_t center = box_mesh_vertex(mesh, (vec3){0}, normal, 0.5f, 0.5f);
    for (int32_t i = 0; i < rim; i ++) {
        float r = i % 2 ? key->f[0] : 1.0f;
        float angle = 1.5707963f + 3.14159265f * (float)i / (float)key->i[0];
        vec3 pos = { r * cosf(angle), r * sinf(angle), 0.0f };
        box_mesh_vertex(mesh, pos, normal, 0.5f + 0.5f * pos[0], 0.5f - 0.5f * pos[1]);
    }

    for (int32_t i = 0; i < rim; i ++) {
        box_mesh_triangle(mesh, center, center + 1 + i, center + 1 + (i + 1) % rim);
    }
}

/** Scale of a Star: the outer radius in X and Y.
 *
 * @param value The Star
 * @param scale The scale
 */
static void box_star_scale(
    const void *value,
    vec3 scale)
{
    const Star *star = value;
    scale[0] = star->radius;
    scale[1] = star->radius;
    scale[2] = 1.0f;
}

/** Construct Star values with the documented inner radius.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_star_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    Star *values = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        values[i] = (Star){ .inner = 0.45f };
    }
}

void box_geometry_star_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, Star, { .ctor = box_star_ctor });
    box_shape_register(world, &(box_shape_desc_t){
        .component = ecs_id(Star),
        .key = box_star_key,
        .generate = box_star_generate,
        .scale = box_star_scale
    });
}
