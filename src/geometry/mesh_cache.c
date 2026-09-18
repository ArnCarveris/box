/**
 * @file geometry/mesh_cache.c
 * @brief Maps mesh keys to renderer handles, generating meshes on first use.
 */

#include "geometry_internal.h"
#include "../render/render_internal.h"

/** Cached mesh entry */
typedef struct box_mesh_entry_t {
    box_mesh_key_t key;  /**< Parameters of the mesh */
    uint32_t handle;     /**< Renderer handle */
    vec3 bounds_min;     /**< Local bounds minimum */
    vec3 bounds_max;     /**< Local bounds maximum */
} box_mesh_entry_t;

/** Mesh cache singleton */
typedef struct GeometryState {
    ecs_vec_t entries;   /**< box_mesh_entry_t */
} GeometryState;

ECS_COMPONENT_DECLARE(GeometryState);

/** Cache destructor.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_geometry_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    GeometryState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &state[i].entries, box_mesh_entry_t);
    }
}

/** Generate the mesh described by a key.
 *
 * @param key The key
 * @param out The mesh to fill
 */
static void box_mesh_generate(
    const box_mesh_key_t *key,
    box_mesh_data_t *out)
{
    switch (key->kind) {
    case BoxShapeBox: box_mesh_box(out); break;
    case BoxShapeQuad: box_mesh_quad(out); break;
    case BoxShapeTriangle: box_mesh_triangle(out); break;
    case BoxShapeRightTriangle: box_mesh_right_triangle(out); break;
    case BoxShapeTrianglePrism: box_mesh_triangle_prism(out); break;
    case BoxShapeRightTrianglePrism: box_mesh_right_triangle_prism(out); break;
    case BoxShapeNGon: box_mesh_ngon(out, key->i0); break;
    case BoxShapeSphere: box_mesh_sphere(out, key->i0, key->smooth); break;
    case BoxShapeHemiSphere: box_mesh_hemisphere(out, key->i0, key->smooth); break;
    case BoxShapeIcoSphere: box_mesh_icosphere(out, key->i0, key->smooth); break;
    case BoxShapeCone: box_mesh_cone(out, key->i0, key->smooth); break;
    case BoxShapeCylinder: box_mesh_cylinder(out, key->i0, key->smooth); break;
    case BoxShapeTorus: box_mesh_torus(out, key->i0, key->i1, key->smooth, key->f0); break;
    case BoxShapeCapsule: box_mesh_capsule(out, key->i0, key->smooth, key->f0); break;
    case BoxShapeTube: box_mesh_tube(out, key->i0, key->smooth, key->f0, key->f1); break;
    case BoxShapeFrustum: box_mesh_frustum(out, key->i0, key->smooth, key->f0); break;
    case BoxShapeRoundedBox:
        box_mesh_rounded_box(out, key->f0, key->f1, key->f2, key->f3, key->i0);
        break;
    }
}

uint32_t box_mesh_cache_get(
    ecs_world_t *world,
    const box_mesh_key_t *key,
    vec3 *bounds_min,
    vec3 *bounds_max)
{
    GeometryState *state = ecs_get_mut(world, ecs_id(GeometryState), GeometryState);
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "box.geometry is not imported");
    box_mesh_entry_t *entry = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->entries) && !entry; i ++) {
        box_mesh_entry_t *candidate = ecs_vec_get_t(&state->entries, box_mesh_entry_t, i);
        if (!memcmp(&candidate->key, key, sizeof(box_mesh_key_t))) {
            entry = candidate;
        }
    }

    if (!entry) {
        box_mesh_data_t data = {0};
        box_mesh_generate(key, &data);
        entry = ecs_vec_append_t(NULL, &state->entries, box_mesh_entry_t);
        entry->key = *key;
        entry->handle = box_render_mesh_create(world, &data);
        entry->bounds_min = data.bounds_min;
        entry->bounds_max = data.bounds_max;
        box_mesh_data_fini(&data);
    }

    *bounds_min = entry->bounds_min;
    *bounds_max = entry->bounds_max;

    return entry->handle;
}

void box_mesh_cache_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, GeometryState);
    ecs_set_hooks(world, GeometryState, {
        .ctor = flecs_default_ctor,
        .dtor = box_geometry_state_dtor
    });
    GeometryState *state = ecs_singleton_ensure(world, GeometryState);
    ecs_vec_init_t(NULL, &state->entries, box_mesh_entry_t, 0);
}
