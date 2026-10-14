/**
 * @file render/draw.c
 * @brief Extracts the shapes of the world into meshes, instances and one batch per entity.
 *
 * Meshes come from the mesh cache and are sent once per backend; the renderer holds a cache
 * reference to every mesh it received, so a handle is not reused before it was destroyed.
 */

#include "render.h"
#include "../geometry/mesh_cache.h"

/** Main thread state of the forward pass extract */
typedef struct box_render_draws_t {
    ecs_query_t *query;         /**< Shapes with their transform, color and scene */
    ecs_map_t meshes;           /**< Mesh handles the backend received */
    ecs_vec_t instances;        /**< box_render_instance_t of the frame */
    ecs_vec_t released;         /**< uint32_t, meshes to release this frame */
    uint32_t capacity;          /**< Instances the instance buffer holds */
    const void *backend;        /**< Context of the backend the meshes were sent to */
    void (*render)(void*, const box_render_packet_t*); /**< Render callback of that backend */
} box_render_draws_t;

/** Color of a shape without Rgb, display encoded */
static const uint8_t box_render_default_color[3] = { 204, 204, 204 };

/** Convert a display encoded channel to linear light.
 *
 * @param value The channel
 * @return The linear value
 */
static float box_render_linear(
    uint8_t value)
{
    float c = (float)value / 255.0f;

    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/** Free the extract state.
 *
 * The mesh references are not released: the mesh cache is freed with the world.
 *
 * @param ctx The state
 */
static void box_render_draws_free(
    void *ctx)
{
    box_render_draws_t *d = ctx;
    ecs_map_fini(&d->meshes);
    ecs_vec_fini_t(NULL, &d->instances, box_render_instance_t);
    ecs_vec_fini_t(NULL, &d->released, uint32_t);
    ecs_os_free(d);
}

/** Forget what an earlier backend received, so meshes and the buffer are sent again.
 *
 * @param world The world
 * @param d The state
 * @param cache The mesh cache
 */
static void box_render_draws_backend(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (backend->ctx == d->backend && backend->render == d->render) {
        goto done;
    }

    ecs_map_iter_t it = ecs_map_iter(&d->meshes);
    while (ecs_map_next(&it)) {
        box_mesh_cache_release(cache, (uint32_t)ecs_map_key(&it));
    }

    ecs_map_clear(&d->meshes);
    d->capacity = 0;
    d->backend = backend->ctx;
    d->render = backend->render;
done:
    return;
}

/** Destroy the meshes only the renderer still references.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 */
static void box_render_draws_release(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    int32_t i;
    ecs_vec_clear(&d->released);
    ecs_map_iter_t it = ecs_map_iter(&d->meshes);
    while (ecs_map_next(&it)) {
        uint32_t handle = (uint32_t)ecs_map_key(&it);
        if (box_mesh_cache_refs(cache, handle) <= 1) {
            *ecs_vec_append_t(NULL, &d->released, uint32_t) = handle;
        }
    }

    for (i = 0; i < ecs_vec_count(&d->released); i ++) {
        uint32_t handle = *ecs_vec_get_t(&d->released, uint32_t, i);
        box_render_packet_resource(p, BOX_RENDER_MESH, BOX_RENDER_DESTROY, handle, 0);
        box_mesh_cache_release(cache, handle);
        ecs_map_remove(&d->meshes, handle);
    }
}

/** Send a mesh to the backend unless it has it, taking a cache reference to it.
 *
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 * @param handle The mesh handle
 * @return Whether the backend has the mesh
 */
static bool box_render_draws_mesh(
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache,
    uint32_t handle)
{
    bool result = true;
    const box_mesh_t *mesh;
    if (ecs_map_get(&d->meshes, handle)) {
        goto done;
    }

    mesh = box_mesh_cache_get(cache, handle);
    if (!mesh || !mesh->vertex_count || !mesh->index_count) {
        result = false;
        goto done;
    }

    uint32_t vertex_size = (uint32_t)mesh->vertex_count * (uint32_t)sizeof(box_vertex_t);
    uint32_t index_size = (uint32_t)mesh->index_count * (uint32_t)sizeof(uint32_t);
    box_render_mesh_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_MESH,
        BOX_RENDER_CREATE, handle,
        (uint32_t)sizeof(box_render_mesh_desc_t) + vertex_size + index_size);
    uint8_t *data = (uint8_t*)(desc + 1);
    desc->vertex_count = (uint32_t)mesh->vertex_count;
    desc->index_count = (uint32_t)mesh->index_count;
    ecs_os_memcpy(data, mesh->vertices, (ecs_size_t)vertex_size);
    ecs_os_memcpy(data + vertex_size, mesh->indices, (ecs_size_t)index_size);
    box_mesh_cache_keep(cache, handle);
    ecs_map_insert(&d->meshes, handle, 1);
done:
    return result;
}

/** Write the instance data of a shape: its model matrix, normal matrix and color.
 *
 * @param out The instance
 * @param transform World transform of the entity, NULL for the identity
 * @param shape Mesh and instance scale of the entity
 * @param color Base color of the entity, NULL for the default color
 */
static void box_render_draws_instance(
    box_render_instance_t *out,
    const WorldTransform *transform,
    const ShapeMesh *shape,
    const Rgb *color)
{
    mat3 m, inv;
    int32_t i;
    if (transform) {
        glm_mat4_copy(ECS_CONST_CAST(vec4*, transform->m), out->model);
    } else {
        glm_mat4_identity(out->model);
    }

    glm_vec4_scale(out->model[0], shape->scale[0], out->model[0]);
    glm_vec4_scale(out->model[1], shape->scale[1], out->model[1]);
    glm_vec4_scale(out->model[2], shape->scale[2], out->model[2]);
    glm_mat4_pick3(out->model, m);
    glm_mat3_inv(m, inv);
    glm_mat3_transpose(inv);
    for (i = 0; i < 3; i ++) {
        glm_vec4(inv[i], 0, out->normal[i]);
    }

    out->color[0] = box_render_linear(color ? color->r : box_render_default_color[0]);
    out->color[1] = box_render_linear(color ? color->g : box_render_default_color[1]);
    out->color[2] = box_render_linear(color ? color->b : box_render_default_color[2]);
    out->color[3] = 1.0f;
}

/** Append a batch of one instance for every shape of a query result.
 *
 * @param it The query result
 * @param p The packet
 * @param d The state
 * @param cache The mesh cache
 */
static void box_render_draws_table(
    ecs_iter_t *it,
    box_render_packet_t *p,
    box_render_draws_t *d,
    box_mesh_cache_t *cache)
{
    const ShapeMesh *shapes = ecs_field(it, ShapeMesh, 0);
    const WorldTransform *transforms = ecs_field(it, WorldTransform, 1);
    const Rgb *colors = ecs_field(it, Rgb, 2);
    bool color_self = ecs_field_is_self(it, 2);
    bool scene_set = ecs_field_is_set(it, 3);
    bool scene_self = ecs_field_is_self(it, 3);
    ecs_entity_t scene = scene_set && !scene_self ? ecs_field_src(it, 3) : 0;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (!shapes[i].mesh || !box_render_draws_mesh(p, d, cache, shapes[i].mesh)) {
            continue;
        }

        uint32_t index = (uint32_t)ecs_vec_count(&d->instances);
        box_render_draws_instance(
            ecs_vec_append_t(NULL, &d->instances, box_render_instance_t),
            transforms ? &transforms[i] : NULL, &shapes[i],
            colors ? &colors[color_self ? i : 0] : NULL);
        box_render_batch_t *b = box_render_packet_batch(p);
        b->scene = scene_set && scene_self ? it->entities[i] : scene;
        b->pipeline = BOX_RENDER_SHADER_MESH;
        b->mesh = shapes[i].mesh;
        b->instances = BOX_RENDER_BUFFER_DRAWS;
        b->first_instance = index;
        b->instance_count = 1;
    }
}

/** Upload the instances of the frame, growing the instance buffer when they do not fit.
 *
 * @param p The packet
 * @param d The state
 */
static void box_render_draws_upload(
    box_render_packet_t *p,
    box_render_draws_t *d)
{
    uint32_t count = (uint32_t)ecs_vec_count(&d->instances);
    uint32_t size = count * (uint32_t)sizeof(box_render_instance_t);
    if (!count) {
        goto done;
    }

    if (count > d->capacity) {
        uint32_t capacity = d->capacity ? d->capacity : 64;
        while (capacity < count) {
            capacity *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_DRAWS, sizeof(box_render_buffer_desc_t));
        desc->size = capacity * (uint32_t)sizeof(box_render_instance_t);
        desc->usage = BOX_RENDER_BUFFER_INSTANCE;
        d->capacity = capacity;
    }

    void *dst = box_render_packet_upload(p, BOX_RENDER_BUFFER_DRAWS, 0, size);
    ecs_os_memcpy(dst, ecs_vec_first(&d->instances), (ecs_size_t)size);
done:
    return;
}

/** Send new meshes, destroy unused ones and add the shapes the views of the frame draw.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractDraws(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    ecs_world_t *world = it->world;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    box_render_packet_t *p = box_render_packet(world);
    if (!cache) {
        goto done;
    }

    box_render_draws_backend(world, d, cache);
    box_render_draws_release(p, d, cache);
    if (!ecs_vec_count(&p->views)) {
        goto done;
    }

    ecs_vec_clear(&d->instances);
    ecs_iter_t qit = ecs_query_iter(world, d->query);
    while (ecs_query_next(&qit)) {
        box_render_draws_table(&qit, p, d, cache);
    }

    box_render_draws_upload(p, d);
done:
    return;
}

void box_render_draws_init(
    ecs_world_t *world)
{
    box_render_draws_t *d = ecs_os_calloc_t(box_render_draws_t);
    ecs_map_init(&d->meshes, NULL);
    ecs_vec_init_t(NULL, &d->instances, box_render_instance_t, 0);
    ecs_vec_init_t(NULL, &d->released, uint32_t, 0);
    d->query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
            {
                .id = ecs_id(Scene),
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractDraws" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractDraws,
        .ctx = d,
        .ctx_free = box_render_draws_free
    });
}
