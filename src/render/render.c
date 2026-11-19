/**
 * @file render/render.c
 * @brief Registers the box.render module, its extract phases, the view extract and the submit.
 */

#define BOX_RENDER_IMPL
#include "environment.h"
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

bool box_render_load(
    const ecs_world_t *world,
    double *busy,
    double *wait)
{
    const RenderRing *ring = ecs_id(RenderRing) ? ecs_singleton_get(world, RenderRing) : NULL;
    bool result = ring && ring->ring;
    *busy = 0;
    *wait = 0;
    if (result) {
        box_render_ring_load(ring->ring, busy, wait);
    }

    return result;
}

box_render_packet_t* box_render_packet(
    ecs_world_t *world)
{
    box_render_ring_t *ring = box_render_get_ring(world);
    box_render_packet_t *result = box_render_ring_current(ring);
    if (!result) {
        const ecs_world_t *real = ecs_get_world(world);
        const ecs_world_info_t *info = ecs_get_world_info(real);
        const App *app = ecs_id(App) ? ecs_singleton_get(real, App) : NULL;
        result = box_render_ring_acquire(ring);
        result->frame = info->frame_count_total;
        result->time = info->world_time_total;
        result->delta = info->delta_time;
        result->fps = ecs_id(App) ? box_app_fps(real) : 0;
        result->headless = app && app->headless;
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

uint32_t box_render_transition_test_node(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    return box_render_transitions_node(box_render_transitions_get(world), entity);
}

bool box_render_transition_test_value(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    void *out)
{
    box_render_transition_component_t c;
    uint32_t node = box_render_transition_test_node(world, entity);
    box_render_transition_table_t *t = box_render_ring_transitions(box_render_get_ring(world));

    return node && box_render_transition_component(component, &c) &&
        box_render_transition_table_value(t, node, c, out);
}

int32_t box_render_transition_test_active(
    ecs_world_t *world)
{
    return box_render_transition_table_active(
        box_render_ring_transitions(box_render_get_ring(world)));
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

/** Order two views by View.order, then by creation, which is the order of their entity ids.
 *
 * @param a The first view
 * @param b The second view
 * @return Negative when a renders first, positive when b does
 */
static int box_render_view_compare(
    const void *a,
    const void *b)
{
    const box_render_view_t *x = a;
    const box_render_view_t *y = b;

    return box_view_compare(x->order, x->view, y->order, y->view);
}

uint32_t box_render_scene_features(
    const ecs_world_t *world,
    ecs_entity_t scene)
{
    const Scene *f = scene && ecs_is_alive(world, scene)
        ? ecs_get(world, scene, Scene)
        : ecs_singleton_get(world, Scene);

    uint32_t result = 0;
    if (!f) {
        goto done;
    }

    result |= f->baked ? BOX_RENDER_FEATURE_BAKED : 0u;
    result |= f->reflections ? BOX_RENDER_FEATURE_REFLECTIONS : 0u;
    result |= f->punctual_shadows ? BOX_RENDER_FEATURE_PUNCTUAL_SHADOWS : 0u;
    result |= f->cookies ? BOX_RENDER_FEATURE_COOKIES : 0u;
    result |= f->environment ? BOX_RENDER_FEATURE_ENVIRONMENT : 0u;
    result |= f->aerial_perspective ? BOX_RENDER_FEATURE_AERIAL : 0u;
    result |= f->sheen ? BOX_RENDER_FEATURE_SHEEN : 0u;
    result |= f->clear_coat ? BOX_RENDER_FEATURE_CLEAR_COAT : 0u;
    result |= f->anisotropy ? BOX_RENDER_FEATURE_ANISOTROPY : 0u;
    result |= f->diffuse_transmission ? BOX_RENDER_FEATURE_DIFFUSE_TRANSMISSION : 0u;
    result |= f->triplanar ? BOX_RENDER_FEATURE_TRIPLANAR : 0u;
    result |= f->fresnel ? BOX_RENDER_FEATURE_FRESNEL : 0u;
    result |= f->transmission ? BOX_RENDER_FEATURE_TRANSMISSION : 0u;
    result |= f->texture_blend ? BOX_RENDER_FEATURE_TEXTURE_BLEND : 0u;
    result |= f->alpha_cutout ? BOX_RENDER_FEATURE_ALPHA_CUTOUT : 0u;
    result |= f->blend_modes ? BOX_RENDER_FEATURE_BLEND_MODES : 0u;
    result |= f->water ? BOX_RENDER_FEATURE_WATER : 0u;
    result |= f->planar_reflections ? BOX_RENDER_FEATURE_PLANAR : 0u;
    result |= f->texture_scroll ? BOX_RENDER_FEATURE_TEXTURE_SCROLL : 0u;
    result |= f->planet_light ? BOX_RENDER_FEATURE_PLANET_LIGHT : 0u;
    result |= f->distortion ? BOX_RENDER_FEATURE_DISTORTION : 0u;
done:
    return result;
}

uint32_t box_render_view_features(
    const ecs_world_t *world,
    ecs_entity_t scene,
    const LightSettings *lighting)
{
    uint32_t result = box_render_scene_features(world, scene) | BOX_RENDER_FEATURE_DEPTH_PREPASS;
    if (lighting) {
        result &= lighting->baked ? ~0u : ~BOX_RENDER_FEATURE_BAKED;
        result &= lighting->reflections ? ~0u : ~BOX_RENDER_FEATURE_REFLECTIONS;
        result &= lighting->atmosphere ? ~0u : ~BOX_RENDER_FEATURE_AERIAL;
        result &= lighting->depth_prepass ? ~0u : ~BOX_RENDER_FEATURE_DEPTH_PREPASS;
    }

    return result;
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

    WorldTransform transform_buf;
    const WorldTransform *transform = box_transform_world(world, camera, &transform_buf);
    box_render_view_t *v = box_render_packet_view(p);
    v->view = e;
    v->order = view->order;
    v->camera = camera;
    v->parent = ecs_get_parent(world, e);
    v->scene = box_render_scene_of(world, camera);
    v->camera_data = *data;
    v->clear = view->clear;
    v->debug = view->debug;
    box_render_lod_settings(&view->lod, &v->lod);
    v->cull.enabled = view->culling.enabled;
    v->cull.occlusion = view->culling.enabled && view->culling.occlusion &&
        view->debug.wireframe != WireframeOnly;
    v->cull.min_dimension = view->culling.min_dimension_px;
    v->cull.bounds = view->culling.enabled && view->culling.bounds;
    v->features = box_render_view_features(world, v->scene, &view->lighting);
    v->scene_features = box_render_scene_features(world, v->scene);
    if (transform) {
        ecs_os_memcpy_t(v->camera_transform, transform->m, mat4);
    } else {
        glm_mat4_identity(v->camera_transform);
    }

    glm_mat4_inv(v->camera_transform, v->view_matrix);
done:
    return;
}

/* LodSettings.distances holds one distance per level */
typedef char box_render_lod_distances_t[
    sizeof(((LodSettings*)0)->distances) == BOX_LOD_LEVELS_MAX * sizeof(float) ? 1 : -1];

void box_render_lod_settings(
    const LodSettings *settings,
    LodSettings *out)
{
    int32_t levels = settings->levels > BOX_LOD_LEVELS_MAX ? BOX_LOD_LEVELS_MAX :
        settings->levels;
    float previous = 0.0f;
    *out = (LodSettings){0};
    out->levels = levels > 1 ? levels : 0;
    for (int32_t i = 1; i < out->levels; i ++) {
        float distance = settings->distances[i];
        distance = isfinite(distance) && distance > 0.0f ? distance : 0.0f;
        out->distances[i] = previous = glm_max(distance, previous);
    }
}

/** Add a view to the packet for every View whose camera is alive and has a Camera, in
 * render order: by View.order, then by creation.
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

    box_render_portals(it->world, it->stage, p);
    box_render_debug_bvh(it->world, p);
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
    ECS_IMPORT(world, BoxTask);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxTerrain);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxEffects);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxTransition);

    ecs_set_name_prefix(world, "Render");
    ECS_META_COMPONENT(world, RenderSettings);
    ECS_META_COMPONENT(world, RenderStatus);
    ECS_COMPONENT_DEFINE(world, RenderRing);
    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, CullStatus);

    ecs_set_hooks(world, RenderRing, {
        .ctor = flecs_default_ctor,
        .dtor = box_render_ring_dtor,
        .move = box_render_ring_move
    });

    RenderExtract = ecs_entity(world, { .name = "Extract" });
    ecs_add_id(world, RenderExtract, EcsPhase);
    ecs_add_pair(world, RenderExtract, EcsDependsOn, EcsOnStore);
    ecs_id(RenderExtract) = RenderExtract;

    RenderSubmit = ecs_entity(world, { .name = "Submit" });
    ecs_add_id(world, RenderSubmit, EcsPhase);
    ecs_add_pair(world, RenderSubmit, EcsDependsOn, RenderExtract);
    ecs_id(RenderSubmit) = RenderSubmit;
    ecs_add_pair(world, TaskPublish, EcsDependsOn, RenderSubmit);

    ecs_singleton_set(world, RenderRing, { .ring = box_render_ring_new() });
    ecs_singleton_set(world, RenderStatus, {0});
    ecs_atfini(world, box_render_fini, NULL);

    ecs_entity_t extract = ecs_entity(world, { .name = "extract" });
    ecs_add_id(world, extract, EcsModule);
    ecs_entity_t dynamic = ecs_entity(world, { .name = "dynamic" });
    ecs_add_id(world, dynamic, EcsModule);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "SettingsOnSet" }),
        .query.terms = {{ .id = ecs_id(RenderSettings) }},
        .events = { EcsOnSet },
        .callback = BoxRenderSettingsOnSet
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "extract.Views" }),
        .phase = RenderExtract,
        .query.terms = {{ .id = ecs_id(View), .inout = EcsIn }},
        .run = BoxRenderExtractViews
    });

    box_render_transitions_init(world);
    box_render_scenes_init(world);
    box_render_captures_init(world);
    box_render_textures_init(world);
    box_render_texture_previews_init(world);
    box_render_draws_init(world);
    box_render_transitions_slots_init(world);
    box_render_atmosphere_init(world);
    box_render_particles_init(world);
    box_render_lights_init(world);
    box_render_planar_init(world);
    box_render_bake_init(world);
    box_render_reflect_init(world);
    box_render_shadows_init(world);
    box_render_cull_status_init(world);
    box_render_effects_init(world);
    box_render_atmosphere_views_init(world);
    box_render_environment_init(world);
    box_render_environment_image_init(world);
    box_render_atmosphere_environment_init(world);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "SubmitPacket" }),
        .phase = RenderSubmit,
        .run = BoxRenderSubmit
    });

    ecs_singleton_set(world, RenderSettings, {
        .in_flight = BOX_RENDER_IN_FLIGHT,
        .main_thread = BOX_RENDER_MAIN_THREAD
    });
}
