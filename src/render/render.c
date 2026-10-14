/**
 * @file render/render.c
 * @brief Registers the box.render module, its extract phases, the view extract and the submit.
 */

#define BOX_RENDER_IMPL
#include "render.h"
#include <stdlib.h>

/** Whether the main thread renders by default: in the browser there is no render thread */
#ifdef __EMSCRIPTEN__
#define BOX_RENDER_MAIN_THREAD (true)
#else
#define BOX_RENDER_MAIN_THREAD (false)
#endif

/** Singleton that owns the packet ring of a world */
typedef struct RenderRing {
    box_render_ring_t *ring; /**< The ring */
} RenderRing;

ECS_COMPONENT_DECLARE(RenderRing);
ECS_DECLARE(RenderExtract);
ECS_DECLARE(RenderSubmit);

/** Stop the render thread and free the ring.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_ring_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderRing *rings = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (rings[i].ring) {
            box_render_ring_free(rings[i].ring);
        }
    }
}

/** Move the ring to another component.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_render_ring_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    RenderRing *d = dst;
    RenderRing *s = src;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        if (d[i].ring && d[i].ring != s[i].ring) {
            box_render_ring_free(d[i].ring);
        }

        d[i].ring = s[i].ring;
        s[i].ring = NULL;
    }
}

/** Get the packet ring of a world.
 *
 * @param world The world or a stage
 * @return The ring
 */
static box_render_ring_t* box_render_get_ring(
    const ecs_world_t *world)
{
    const RenderRing *ring = ecs_singleton_get(world, RenderRing);
    ecs_assert(ring != NULL, ECS_INVALID_OPERATION, "box.render is not imported");

    return ring->ring;
}

box_render_packet_t* box_render_packet(
    ecs_world_t *world)
{
    box_render_ring_t *ring = box_render_get_ring(world);
    box_render_packet_t *result = box_render_ring_current(ring);
    if (!result) {
        const ecs_world_info_t *info = ecs_get_world_info(ecs_get_world(world));
        result = box_render_ring_acquire(ring);
        result->frame = info->frame_count_total;
        result->time = info->world_time_total;
        result->delta = info->delta_time;
    }

    return result;
}

void box_render_set_backend(
    ecs_world_t *world,
    const box_render_backend_t *backend)
{
    box_render_ring_set_backend(box_render_get_ring(world), backend);
}

const box_render_backend_t* box_render_get_backend(
    const ecs_world_t *world)
{
    return box_render_ring_backend(box_render_get_ring(world));
}

void box_render_use_gpu(
    ecs_world_t *world)
{
    box_render_ring_t *ring = box_render_get_ring(world);
#ifdef BOX_RENDER_WGPU
    if (box_render_ring_is_default(ring)) {
        box_render_backend_t backend = box_render_gpu_backend();
        box_render_ring_set_backend(ring, &backend);
    }
#else
    (void)ring;
#endif
}

void box_render_pause(
    ecs_world_t *world,
    bool paused)
{
    box_render_ring_pause(box_render_get_ring(world), paused);
}

void box_render_sync(
    ecs_world_t *world)
{
    box_render_ring_t *ring = box_render_get_ring(world);
    RenderStatus *status = ecs_singleton_get_mut(world, RenderStatus);
    box_render_ring_sync(ring);
    if (status) {
        box_render_ring_status(ring, status);
    }
}

/** Wait for the packets in flight before the world is deleted, so that the captures they
 * write land before the fini actions registered later, such as the removal of
 * AppStatus.tmp_dir, run.
 *
 * @param world The world
 * @param ctx Unused
 */
static void box_render_fini(
    ecs_world_t *world,
    void *ctx)
{
    (void)ctx;
    box_render_ring_sync(box_render_get_ring(world));
}

/** Apply new render settings before the next frame acquires its packet.
 *
 * @param it The iterator of the observer
 */
static void BoxRenderSettingsOnSet(
    ecs_iter_t *it)
{
    const RenderSettings *settings = ecs_field(it, RenderSettings, 0);
    box_render_ring_configure(box_render_get_ring(it->world), settings);
}

/** Order two views by creation, which is the order of their entity ids.
 *
 * @param a The first view
 * @param b The second view
 * @return Negative when a renders first, positive when b does
 */
static int box_render_view_compare(
    const void *a,
    const void *b)
{
    ecs_entity_t x = ((const box_render_view_t*)a)->view;
    ecs_entity_t y = ((const box_render_view_t*)b)->view;

    return (x > y) - (x < y);
}

/** Describe a view whose camera has a Camera in the packet.
 *
 * @param world The world
 * @param p The packet
 * @param e The view entity
 * @param view The view
 */
static void box_render_extract_view(
    ecs_world_t *world,
    box_render_packet_t *p,
    ecs_entity_t e,
    const View *view)
{
    ecs_entity_t camera = view->camera;
    const Camera *data = camera && ecs_is_alive(world, camera)
        ? ecs_get(world, camera, Camera)
        : NULL;
    if (!data) {
        goto done;
    }

    const WorldTransform *transform = ecs_get(world, camera, WorldTransform);
    box_render_view_t *v = box_render_packet_view(p);
    v->view = e;
    v->camera = camera;
    v->parent = ecs_get_parent(world, e);
    v->scene = box_render_scene_of(world, camera);
    v->camera_data = *data;
    v->clear = view->clear;
    v->cull.enabled = !view->culling.disabled;
    v->cull.occlusion = v->cull.enabled && !view->culling.frustum_only;
    v->cull.min_dimension = view->culling.min_dimension_px == 0
        ? BOX_RENDER_CULL_MIN_DIMENSION
        : glm_max(view->culling.min_dimension_px, 0.0f);
    if (transform) {
        ecs_os_memcpy_t(v->camera_transform, transform->m, mat4);
    } else {
        glm_mat4_identity(v->camera_transform);
    }

    glm_mat4_inv(v->camera_transform, v->view_matrix);
done:
    return;
}

/** Add a view to the packet for every View whose camera is alive and has a Camera, in
 * creation order.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractViews(
    ecs_iter_t *it)
{
    box_render_packet_t *p = box_render_packet(it->world);
    int32_t i, count;
    while (ecs_iter_next(it)) {
        const View *views = ecs_field(it, View, 0);
        for (i = 0; i < it->count; i ++) {
            box_render_extract_view(it->world, p, it->entities[i], &views[i]);
        }
    }

    count = ecs_vec_count(&p->views);
    if (count > 1) {
        qsort(ecs_vec_first(&p->views), (size_t)count, sizeof(box_render_view_t),
            box_render_view_compare);
    }
}

/** Hand the packet of the frame to the renderer and publish the ring counters.
 *
 * @param it The iterator of the system
 */
static void BoxRenderSubmit(
    ecs_iter_t *it)
{
    box_render_ring_t *ring = box_render_get_ring(it->world);
    RenderStatus *status = ecs_singleton_get_mut(it->world, RenderStatus);
    box_render_packet(it->world);
    box_render_ring_submit(ring);
    if (status) {
        box_render_ring_status(ring, status);
    }
}

void BoxRenderImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxRender);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLight);

    ECS_META_COMPONENT(world, RenderSettings);
    ECS_META_COMPONENT(world, RenderStatus);
    ECS_META_COMPONENT(world, CullStatus);
    ECS_COMPONENT_DEFINE(world, RenderRing);

    ecs_set_hooks(world, RenderRing, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_ring_dtor,
        .move = box_render_ring_move
    });

    RenderExtract = ecs_entity(world, { .name = "RenderExtract" });
    ecs_add_id(world, RenderExtract, EcsPhase);
    ecs_add_pair(world, RenderExtract, EcsDependsOn, EcsOnStore);
    ecs_id(RenderExtract) = RenderExtract;

    RenderSubmit = ecs_entity(world, { .name = "RenderSubmit" });
    ecs_add_id(world, RenderSubmit, EcsPhase);
    ecs_add_pair(world, RenderSubmit, EcsDependsOn, RenderExtract);
    ecs_id(RenderSubmit) = RenderSubmit;

    ecs_singleton_set(world, RenderRing, { .ring = box_render_ring_new() });
    ecs_singleton_set(world, RenderStatus, {0});
    ecs_atfini(world, box_render_fini, NULL);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "RenderSettingsOnSet" }),
        .query.terms = {{ .id = ecs_id(RenderSettings) }},
        .events = { EcsOnSet },
        .callback = BoxRenderSettingsOnSet
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractViews" }),
        .phase = RenderExtract,
        .query.terms = {{ .id = ecs_id(View), .inout = EcsIn }},
        .run = BoxRenderExtractViews
    });

    box_render_scenes_init(world);
    box_render_captures_init(world);
    box_render_textures_init(world);
    box_render_draws_init(world);
    box_render_lights_init(world);
    box_render_shadows_init(world);
    box_render_cull_status_init(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderSubmitPacket" }),
        .phase = RenderSubmit,
        .run = BoxRenderSubmit
    });

    ecs_singleton_set(world, RenderSettings, {
        .in_flight = BOX_RENDER_IN_FLIGHT,
        .main_thread = BOX_RENDER_MAIN_THREAD
    });
}
