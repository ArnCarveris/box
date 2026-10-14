/**
 * @file render/render.c
 * @brief Registers the box.render module, its extract phases, the view extract and the submit.
 */

#define BOX_RENDER_IMPL
#include "render.h"

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

/** Add a view to the packet for every View whose camera is alive and has a Camera.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractViews(
    ecs_iter_t *it)
{
    const View *views = ecs_field(it, View, 0);
    box_render_packet_t *p = box_render_packet(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t camera = views[i].camera;
        const Camera *data = camera && ecs_is_alive(it->world, camera)
            ? ecs_get(it->world, camera, Camera)
            : NULL;
        if (!data) {
            continue;
        }

        const WorldTransform *transform = ecs_get(it->world, camera, WorldTransform);
        box_render_view_t *view = box_render_packet_view(p);
        view->view = it->entities[i];
        view->camera = camera;
        view->camera_data = *data;
        view->clear = views[i].clear;
        if (transform) {
            ecs_os_memcpy_t(view->camera_transform, transform->m, mat4);
        } else {
            glm_mat4_identity(view->camera_transform);
        }
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
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);

    ECS_META_COMPONENT(world, RenderSettings);
    ECS_META_COMPONENT(world, RenderStatus);
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
        .callback = BoxRenderExtractViews
    });

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
