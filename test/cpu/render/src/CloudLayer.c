/**
 * @file CloudLayer.c
 * @brief Tests for CloudLayer: its defaults, the coverage remap and the layers the views of a
 * packet carry for the sky pass.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"

/** A single channel image the layers read, loaded so the null backend has it */
#define CLOUD_PNG "test/assets/textures/cookie_gray.png"

/** Seconds of a frame */
#define DT (1.0f / 60.0f)

/** Create a world that renders with the null backend, with a scene root that has the default
 * Atmosphere and a view whose camera is in it.
 *
 * @param null Receives the backend
 * @param scene Receives the scene
 * @param camera Receives the camera
 * @return The world
 */
static ecs_world_t* cloud_world(
    box_render_null_t **null,
    ecs_entity_t *scene,
    ecs_entity_t *camera)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    *scene = ecs_new(world);
    ecs_add(world, *scene, Scene);
    ecs_add(world, *scene, Atmosphere);
    *camera = ecs_new_w_pair(world, EcsChildOf, *scene);
    ecs_set(world, *camera, Position, {0, 2, 10});
    ecs_set(world, *camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_entity(world, { .name = "view" });
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = *camera;
    ecs_modified(world, view, View);

    return world;
}

/** Run a frame of DT seconds and get the only view of the packet the backend received.
 *
 * @param world The world
 * @param null The backend
 * @param time The total time, advanced by the frame
 * @return The view
 */
static const box_render_view_t* cloud_frame(
    ecs_world_t *world,
    box_render_null_t *null,
    double *time)
{
    ecs_progress(world, DT);
    box_render_sync(world);
    *time += (double)DT;
    test_int(ecs_vec_count(&null->last.views), 1);

    return ecs_vec_first(&null->last.views);
}

/** Create a single channel texture and run frames until the backend has it. The image loads on
 * a job thread, so a frame waits a millisecond to give it time on a busy machine.
 *
 * @param world The world
 * @param null The backend
 * @param time The total time, advanced by the frames
 * @return The texture entity
 */
static ecs_entity_t cloud_texture(
    ecs_world_t *world,
    box_render_null_t *null,
    double *time)
{
    ecs_entity_t result = ecs_new(world);
    int32_t i;
    ecs_set(world, result, TexturePath, {.path = ECS_CONST_CAST(char*, CLOUD_PNG),
        .single_channel = true});
    for (i = 0; i < 500; i ++) {
        cloud_frame(world, null, time);
        if (box_textures_handle(box_textures_get(world), result)) {
            break;
        }

        ecs_os_sleep(0, 1000 * 1000);
    }

    test_assert(box_textures_handle(box_textures_get(world), result) != 0);

    return result;
}

/** Add a layer with a texture at an altitude to an entity of a scene.
 *
 * @param world The world
 * @param scene The scene
 * @param texture The texture
 * @param altitude World Y of the layer
 * @return The entity of the layer
 */
static ecs_entity_t cloud_layer(
    ecs_world_t *world,
    ecs_entity_t scene,
    ecs_entity_t texture,
    float altitude)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_add(world, result, CloudLayer);
    CloudLayer *c = ecs_get_mut(world, result, CloudLayer);
    c->texture = texture;
    c->altitude = altitude;
    ecs_modified(world, result, CloudLayer);

    return result;
}

void CloudLayer_defaults(void) {
    // atmosphere/clouds
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, CloudLayer);
    const CloudLayer *c = ecs_get(world, e, CloudLayer);
    test_assert(c != NULL);
    test_uint(c->texture, 0);
    test_flt(c->size, 4000.0f);
    test_flt(c->altitude, 1500.0f);
    test_flt(c->offset[0], 0.0f);
    test_flt(c->offset[1], 0.0f);
    test_flt(c->velocity[0], 0.0f);
    test_flt(c->velocity[1], 0.0f);
    test_flt(c->threshold, 0.55f);
    test_flt(c->softness, 0.15f);
    test_flt(c->density, 1.0f);
    test_int(c->tint.r, 255);
    test_int(c->tint.g, 255);
    test_int(c->tint.b, 255);

    test_assert(ecs_script_run(world, "clouds",
        "using box.atmosphere\n"
        "layer { CloudLayer: {altitude: 800} }\n", NULL) == 0);
    c = ecs_get(world, ecs_lookup(world, "layer"), CloudLayer);
    test_assert(c != NULL);
    test_flt(c->altitude, 800.0f);
    test_flt(c->size, 4000.0f);
    test_flt(c->threshold, 0.55f);
    test_flt(c->density, 1.0f);
    test_int(c->tint.r, 255);

    ecs_fini(world);
}

void CloudLayer_coverage_remap(void) {
    // atmosphere/clouds
    test_flt(box_render_cloud_coverage(0.55f, 0.55f, 0.15f), 0.5f);
    test_assert(fabsf(box_render_cloud_coverage(0.4f, 0.55f, 0.15f)) < 1e-5f);
    test_assert(fabsf(box_render_cloud_coverage(0.7f, 0.55f, 0.15f) - 1.0f) < 1e-5f);
    test_assert(fabsf(box_render_cloud_coverage(0.625f, 0.55f, 0.15f) - 0.75f) < 1e-5f);
    test_flt(box_render_cloud_coverage(0.0f, 0.55f, 0.15f), 0.0f);
    test_flt(box_render_cloud_coverage(1.0f, 0.55f, 0.15f), 1.0f);

    /* A softness of 0 is a step at the threshold, finite everywhere */
    test_flt(box_render_cloud_coverage(0.5f, 0.5f, 0.0f), 0.5f);
    test_flt(box_render_cloud_coverage(0.49f, 0.5f, 0.0f), 0.0f);
    test_flt(box_render_cloud_coverage(0.51f, 0.5f, 0.0f), 1.0f);
    test_assert(isfinite(box_render_cloud_coverage(0.5f, 0.5f, 0.0f)));

    test_flt(box_render_cloud_alpha(1.0f, 1.0f), 1.0f);
    test_flt(box_render_cloud_alpha(1.0f, 0.0f), 0.0f);
    test_flt(box_render_cloud_alpha(0.5f, 0.5f), 0.25f);
    test_flt(box_render_cloud_alpha(1.0f, 3.0f), 1.0f);
}

void CloudLayer_packet_view_layer(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene, camera;
    double time = 0;
    ecs_world_t *world = cloud_world(&null, &scene, &camera);
    ecs_entity_t tex = cloud_texture(world, null, &time);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1200);
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->size = 500;
    c->threshold = 0.4f;
    c->softness = 0.1f;
    c->density = 0.8f;
    c->tint = (rgb){255, 0, 0};
    ecs_modified(world, e, CloudLayer);

    const box_render_view_t *v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 1);
    const box_render_cloud_layer_t *l = &v->atmos.clouds[0];
    test_uint(l->texture, box_textures_handle(box_textures_get(world), tex));
    test_uint(l->entity, e);
    test_flt(l->inv_size, 1.0f / 500.0f);
    test_flt(l->altitude, 1200.0f);
    test_flt(l->height, 1198.0f);
    test_flt(l->threshold, 0.4f);
    test_flt(l->softness, 0.1f);
    test_flt(l->density, 0.8f);
    test_flt(l->tint[0], 1.0f);
    test_flt(l->tint[1], 0.0f);
    test_flt(l->origin[0], 0.0f);
    test_assert(fabsf(l->origin[1] - 10.0f / 500.0f) < 1e-6f);

    /* A view that does not render the atmosphere has no clouds */
    ecs_entity_t view = ecs_lookup(world, "view");
    ecs_get_mut(world, view, View)->lighting.atmosphere = false;
    ecs_modified(world, view, View);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 0);
    ecs_get_mut(world, view, View)->lighting.atmosphere = true;
    ecs_modified(world, view, View);

    /* Neither has a layer without a texture or without a size */
    ecs_get_mut(world, e, CloudLayer)->texture = 0;
    ecs_modified(world, e, CloudLayer);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 0);
    c = ecs_get_mut(world, e, CloudLayer);
    c->texture = tex;
    c->size = 0;
    ecs_modified(world, e, CloudLayer);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 0);

    /* Nor a scene without an Atmosphere */
    ecs_get_mut(world, e, CloudLayer)->size = 500;
    ecs_modified(world, e, CloudLayer);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 1);
    ecs_remove(world, scene, Atmosphere);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 0);

    ecs_fini(world);
}

void CloudLayer_layer_on_atmosphere_entity(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene, camera;
    double time = 0;
    ecs_world_t *world = cloud_world(&null, &scene, &camera);
    ecs_entity_t tex = cloud_texture(world, null, &time);
    ecs_add(world, scene, CloudLayer);
    ecs_get_mut(world, scene, CloudLayer)->texture = tex;
    ecs_modified(world, scene, CloudLayer);

    const box_render_view_t *v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 1);
    test_uint(v->atmos.clouds[0].entity, scene);
    test_flt(v->atmos.clouds[0].altitude, 1500.0f);

    ecs_fini(world);
}

void CloudLayer_offset_wraps_at_large_time(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene, camera;
    double time = 0;
    ecs_world_t *world = cloud_world(&null, &scene, &camera);
    ecs_entity_t tex = cloud_texture(world, null, &time);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->offset[0] = 123.5f;
    c->velocity[0] = 13.75f;
    c->velocity[1] = -5.25f;
    ecs_modified(world, e, CloudLayer);
    ecs_set(world, camera, Position, {1000000.0f, 2, -250000.0f});

    /* Ten simulated hours in one frame */
    ecs_progress(world, 36000.0f);
    box_render_sync(world);
    time += (double)36000.0f;
    const box_render_view_t *v = ecs_vec_first(&null->last.views);
    test_int(v->atmos.cloud_count, 1);
    const box_render_cloud_layer_t *l = &v->atmos.clouds[0];
    double x = fmod(123.5 + 13.75 * time, 4000.0);
    double z = fmod(-5.25 * time, 4000.0) + 4000.0;
    test_assert(l->offset[0] >= 0 && l->offset[0] < 4000);
    test_assert(l->offset[1] >= 0 && l->offset[1] < 4000);
    test_assert(fabs((double)l->offset[0] - x) < 1e-2);
    test_assert(fabs((double)l->offset[1] - z) < 1e-2);

    /* The texture coordinate of a far camera stays precise */
    double u = (1000000.0 + (double)l->offset[0]) / 4000.0;
    double w = (-250000.0 + (double)l->offset[1]) / 4000.0;
    u -= floor(u);
    w -= floor(w);
    test_assert(l->origin[0] >= 0 && l->origin[0] < 1);
    test_assert(l->origin[1] >= 0 && l->origin[1] < 1);
    test_assert(fabs((double)l->origin[0] - u) < 1e-6);
    test_assert(fabs((double)l->origin[1] - w) < 1e-6);

    /* As the light cookie computes it */
    LightCookie wind = { .size = 4000, .offset = {123.5f, 0}, .velocity = {13.75f, -5.25f} };
    float out[2];
    box_render_cookie_offset(&wind, time, out);
    test_flt(l->offset[0], out[0]);
    test_flt(l->offset[1], out[1]);

    /* A frame later the pattern moved by velocity times the frame */
    double before = (double)l->offset[0];
    v = cloud_frame(world, null, &time);
    double dx = (double)v->atmos.clouds[0].offset[0] - before;
    test_assert(fabs(dx - 13.75 * (double)DT) < 2e-3);

    ecs_fini(world);
}

void CloudLayer_layer_limit_and_order(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene, camera;
    double time = 0;
    ecs_world_t *world = cloud_world(&null, &scene, &camera);
    ecs_entity_t tex = cloud_texture(world, null, &time);
    float altitudes[5] = {3000, 500, 2000, 1000, 100};
    ecs_entity_t layers[5];
    int32_t i;
    for (i = 0; i < 5; i ++) {
        layers[i] = cloud_layer(world, scene, tex, altitudes[i]);
    }

    /* The 4 lowest entity ids, by ascending altitude: the last layer, at 100, is ignored with a
     * warning */
    int level = ecs_log_set_level(-4);
    const box_render_view_t *v = cloud_frame(world, null, &time);
    ecs_log_set_level(level);
    test_int(v->atmos.cloud_count, BOX_CLOUD_LAYER_MAX);
    test_uint(v->atmos.clouds[0].entity, layers[1]);
    test_uint(v->atmos.clouds[1].entity, layers[3]);
    test_uint(v->atmos.clouds[2].entity, layers[2]);
    test_uint(v->atmos.clouds[3].entity, layers[0]);
    for (i = 1; i < BOX_CLOUD_LAYER_MAX; i ++) {
        test_assert(v->atmos.clouds[i - 1].altitude < v->atmos.clouds[i].altitude);
    }

    /* Removing a layer lets the ignored one in */
    ecs_delete(world, layers[2]);
    v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, BOX_CLOUD_LAYER_MAX);
    test_uint(v->atmos.clouds[0].entity, layers[4]);
    test_flt(v->atmos.clouds[0].altitude, 100.0f);
    test_uint(v->atmos.clouds[1].entity, layers[1]);
    test_uint(v->atmos.clouds[2].entity, layers[3]);
    test_uint(v->atmos.clouds[3].entity, layers[0]);

    ecs_fini(world);
}

void CloudLayer_layers_of_other_scene_ignored(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene, camera;
    double time = 0;
    ecs_world_t *world = cloud_world(&null, &scene, &camera);
    ecs_entity_t tex = cloud_texture(world, null, &time);
    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, Scene);
    cloud_layer(world, other, tex, 1500);

    const box_render_view_t *v = cloud_frame(world, null, &time);
    test_int(v->atmos.cloud_count, 0);

    ecs_fini(world);
}
