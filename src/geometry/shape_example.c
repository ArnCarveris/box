/**
 * @file geometry/shape_example.c
 * @brief Worked example of the shape registry: the flat Star polygon.
 *
 * Star is registered through the same public API a project uses, so this file
 * is the reference for adding a shape without patching the engine. A shape
 * needs three things: a component with its parameters, a key function that
 * splits those parameters into what changes the mesh and what only scales it,
 * and a build function that fills the mesh of a key. Everything else, the
 * observer, the mesh cache, the sharing and the instanced draw, is the module's.
 */

#include "geometry_internal.h"
#include <math.h>

/** Full turn as a float constant */
#define BOX_STAR_TAU (6.28318530717958647692f)

/** Inner radius ratio a star without one falls back to */
#define BOX_STAR_INNER (0.45f)

/** Split a Star into the key of its mesh and the scale of its instance.
 *
 * The point count and the inner ratio change the outline, so they go in the
 * key; the radius only scales it, so every radius shares one mesh.
 *
 * @param value The Star component
 * @param key The key to fill
 * @param scale The instance scale to fill
 */
static void box_star_key(
    const void *value,
    box_shape_key_t *key,
    vec3 *scale)
{
    const Star *star = value;
    float radius = star->radius > 0.0f ? star->radius : 1.0f;

    key->i0 = star->points;
    key->f0 = star->inner > 0.0f ? star->inner : BOX_STAR_INNER;
    *scale = vec3_new(radius, radius, 1.0f);
}

/** Build a unit radius star in the XY plane as a fan around its center.
 *
 * @param key The key, i0 holds the points and f0 the inner radius ratio
 * @param mesh The mesh to fill
 */
static void box_star_build(
    const box_shape_key_t *key,
    box_shape_mesh_t *mesh)
{
    int32_t points = key->i0 < 3 ? 3 : key->i0;
    int32_t rim = points * 2;
    float inner = key->f0;
    vec3 normal = vec3_new(0.0f, 0.0f, -1.0f);
    uint32_t center;
    int32_t i;

    box_shape_reserve(mesh, rim + 1, rim * 3);
    center = box_shape_vertex(mesh, vec3_new(0.0f, 0.0f, 0.0f), normal, 0.5f, 0.5f);
    for (i = 0; i < rim; i ++) {
        float angle = (float)i / (float)rim * BOX_STAR_TAU;
        float radius = (i % 2) ? inner : 1.0f;
        float x = cosf(angle) * radius;
        float y = sinf(angle) * radius;

        box_shape_vertex(mesh, vec3_new(x, y, 0.0f), normal, x * 0.5f + 0.5f, y * 0.5f + 0.5f);
    }

    for (i = 0; i < rim; i ++) {
        uint32_t a = (uint32_t)(i + 1);
        uint32_t b = (uint32_t)((i + 1) % rim + 1);

        box_shape_triangle(mesh, center, b, a);
    }
}

void box_shape_example_init(
    ecs_world_t *world)
{
    ecs_entity_t shape = ecs_entity(world, { .name = "star" });

    ecs_add_pair(world, ecs_id(Star), EcsOnInstantiate, EcsInherit);

    ecs_set(world, shape, Shape, {
        .component = ecs_id(Star),
        .key = box_star_key,
        .build = box_star_build
    });
}
