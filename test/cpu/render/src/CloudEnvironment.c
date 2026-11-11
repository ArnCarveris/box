/**
 * @file CloudEnvironment.c
 * @brief Tests for the cloud layers in the environment of an Atmosphere: the layers its renders
 * carry, when moving layers render it again, and that such moves capture no probes again.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "draw.h"
#include "environment.h"
#include "bake.h"

/** A single channel image the layers read, loaded so the null backend has it */
#define CLOUD_PNG "test/assets/textures/cookie_gray.png"

/** Seconds of a frame */
#define DT (1.0f / 60.0f)

/** Create a world that renders with the null backend, with a scene root that has the default
 * Atmosphere, a sun and a lit view whose camera is in it, lit by the Atmosphere.
 *
 * @param null Receives the backend
 * @param scene Receives the scene
 * @return The world
 */
static ecs_world_t* env_world(
    box_render_null_t **null,
    ecs_entity_t *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    *scene = ecs_new(world);
    ecs_add(world, *scene, Scene);
    ecs_add(world, *scene, Atmosphere);
    ecs_entity_t sun = ecs_new_w_pair(world, EcsChildOf, *scene);
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_set(world, sun, Rgb, {255, 255, 255});
    ecs_set(world, sun, Rotation, {glm_rad(60.0f), glm_rad(30.0f), 0});
    ecs_get_mut(world, *scene, Atmosphere)->sun = sun;
    ecs_modified(world, *scene, Atmosphere);
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, *scene);
    ecs_set(world, camera, Position, {0, 2, 10});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->lighting.enabled = true;
    v->lighting.environment.source = *scene;
    ecs_modified(world, view, View);

    return world;
}

/** Run a frame of DT seconds and wait until the backend received it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, DT);
    box_render_sync(world);
}

/** Count the renders of the sky of an atmosphere into a cubemap in the last packet.
 *
 * @param null The backend
 * @param out Receives the last one, may be NULL
 * @return The renders
 */
static int32_t renders(
    box_render_null_t *null,
    box_render_atmosphere_env_t *out)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_ATMOSPHERE_ENVIRONMENT) {
            test_assert(r->size >= sizeof(box_render_atmosphere_env_t));
            if (out) {
                *out = *(const box_render_atmosphere_env_t*)box_render_packet_data(
                    &null->last, r->data);
            }

            result ++;
        }
    }

    return result;
}

/** Run a frame and count the renders of the environment in it.
 *
 * @param world The world
 * @param null The backend
 * @param out Receives the last one, may be NULL
 * @return The renders
 */
static int32_t frame_renders(
    ecs_world_t *world,
    box_render_null_t *null,
    box_render_atmosphere_env_t *out)
{
    frame(world);

    return renders(null, out);
}

/** Create a single channel texture and run frames until the backend has it, then until the
 * environment no longer renders.
 *
 * @param world The world
 * @param null The backend
 * @return The texture entity
 */
static ecs_entity_t cloud_texture(
    ecs_world_t *world,
    box_render_null_t *null)
{
    ecs_entity_t result = ecs_new(world);
    int32_t i;
    ecs_set(world, result, TexturePath, {.path = ECS_CONST_CAST(char*, CLOUD_PNG),
        .single_channel = true});
    for (i = 0; i < 200; i ++) {
        frame(world);
        if (box_textures_handle(box_textures_get(world), result)) {
            break;
        }
    }

    test_assert(box_textures_handle(box_textures_get(world), result) != 0);
    frame(world);
    test_int(frame_renders(world, null, NULL), 0);

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

/** Set the offset of a layer.
 *
 * @param world The world
 * @param e The layer
 * @param x Offset along X
 * @param z Offset along Z
 */
static void cloud_offset(
    ecs_world_t *world,
    ecs_entity_t e,
    float x,
    float z)
{
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->offset[0] = x;
    c->offset[1] = z;
    ecs_modified(world, e, CloudLayer);
}

/** Get the EnvironmentStatus.version of a source.
 *
 * @param world The world
 * @param e The source
 * @return The version
 */
static int32_t version_of(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EnvironmentStatus *status = ecs_get(world, e, EnvironmentStatus);
    test_assert(status != NULL);

    return status->version;
}

/** Find the last fill or create of a probe volume in the last packet.
 *
 * @param null The backend
 * @return Whether the packet fills a volume
 */
static bool volume_fill(
    box_render_null_t *null)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        const box_render_volume_desc_t *desc = r->kind == BOX_RENDER_VOLUME &&
            r->op != BOX_RENDER_DESTROY ? box_render_packet_data(&null->last, r->data) : NULL;
        if (desc && (r->op == BOX_RENDER_CREATE || desc->op == BOX_RENDER_VOLUME_FILL)) {
            result = true;
        }
    }

    return result;
}

void CloudEnvironment_moved_rule(void) {
    // atmosphere/clouds
    float zero[2] = {0, 0};

    /* 1/64 of 4000 is 62.5 */
    test_bool(box_render_cloud_moved(zero, (float[2]){62, 0}, 4000), false);
    test_bool(box_render_cloud_moved(zero, (float[2]){63, 0}, 4000), true);
    test_bool(box_render_cloud_moved(zero, (float[2]){0, 63}, 4000), true);
    test_bool(box_render_cloud_moved(zero, (float[2]){50, 50}, 4000), true);
    test_bool(box_render_cloud_moved(zero, (float[2]){40, 40}, 4000), false);

    /* An offset that wrapped around the size moved the short way */
    test_bool(box_render_cloud_moved((float[2]){3990, 0}, (float[2]){50, 0}, 4000), false);
    test_bool(box_render_cloud_moved((float[2]){3990, 0}, (float[2]){60, 0}, 4000), true);
    test_bool(box_render_cloud_moved((float[2]){10, 0}, (float[2]){3960, 0}, 4000), false);
    test_bool(box_render_cloud_moved((float[2]){0, 3999}, (float[2]){0, 1}, 4000), false);

    /* The fraction follows the size */
    test_bool(box_render_cloud_moved(zero, (float[2]){1, 0}, 64), false);
    test_bool(box_render_cloud_moved(zero, (float[2]){1.1f, 0}, 64), true);

    /* No size never moves */
    test_bool(box_render_cloud_moved(zero, (float[2]){100, 0}, 0), false);
}

void CloudEnvironment_layer_added_renders(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    box_render_atmosphere_env_t desc;
    int32_t version = version_of(world, scene);

    /* A layer without a texture adds nothing */
    ecs_entity_t e = cloud_layer(world, scene, 0, 1200);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_get_mut(world, e, CloudLayer)->texture = tex;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, &desc), 1);
    test_int(version_of(world, scene), version + 1);
    test_int(desc.cloud_count, 1);
    test_uint(desc.clouds[0].entity, e);
    test_uint(desc.clouds[0].texture, box_textures_handle(box_textures_get(world), tex));
    test_flt(desc.clouds[0].altitude, 1200.0f);
    test_flt(desc.km_per_unit, 0.001f);
    test_flt(desc.aerial_intensity, 1.0f);

    /* Seen from world X and Z 0 at the sea level */
    test_flt(desc.clouds[0].height, 1200.0f);
    test_flt(desc.clouds[0].origin[0], 0.0f);
    test_flt(desc.clouds[0].origin[1], 0.0f);

    test_int(frame_renders(world, null, NULL), 0);
    test_int(frame_renders(world, null, NULL), 0);

    /* A second layer renders again */
    cloud_layer(world, scene, tex, 3000);
    test_int(frame_renders(world, null, &desc), 1);
    test_int(desc.cloud_count, 2);
    test_flt(desc.clouds[1].altitude, 3000.0f);
    test_int(frame_renders(world, null, NULL), 0);

    /* The sea level moves the capture point */
    ecs_get_mut(world, scene, Atmosphere)->sea_level = 200;
    ecs_modified(world, scene, Atmosphere);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].height, 1000.0f);

    ecs_fini(world);
}

void CloudEnvironment_layer_changed_renders(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    box_render_atmosphere_env_t desc;
    test_int(frame_renders(world, null, NULL), 1);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_get_mut(world, e, CloudLayer)->density = 0.5f;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].density, 0.5f);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_get_mut(world, e, CloudLayer)->threshold = 0.3f;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 1);

    ecs_get_mut(world, e, CloudLayer)->tint = (rgb){255, 128, 0};
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 1);

    ecs_get_mut(world, e, CloudLayer)->altitude = 1600;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 1);

    ecs_get_mut(world, e, CloudLayer)->size = 2000;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].inv_size, 1.0f / 2000.0f);

    /* Setting the same values renders nothing */
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_fini(world);
}

void CloudEnvironment_layer_removed_renders(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e1 = cloud_layer(world, scene, tex, 1500);
    ecs_entity_t e2 = cloud_layer(world, scene, tex, 2500);
    box_render_atmosphere_env_t desc;
    test_int(frame_renders(world, null, &desc), 1);
    test_int(desc.cloud_count, 2);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_remove(world, e2, CloudLayer);
    test_int(frame_renders(world, null, &desc), 1);
    test_int(desc.cloud_count, 1);
    test_uint(desc.clouds[0].entity, e1);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_delete(world, e1);
    test_int(frame_renders(world, null, &desc), 1);
    test_int(desc.cloud_count, 0);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_fini(world);
}

void CloudEnvironment_move_past_fraction_renders(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    box_render_atmosphere_env_t desc;
    test_int(frame_renders(world, null, NULL), 1);
    test_int(frame_renders(world, null, NULL), 0);

    /* 1/64 of the default size of 4000 is 62.5 */
    cloud_offset(world, e, 40, 0);
    test_int(frame_renders(world, null, NULL), 0);
    cloud_offset(world, e, 60, 0);
    test_int(frame_renders(world, null, NULL), 0);
    cloud_offset(world, e, 70, 0);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].offset[0], 70.0f);
    test_assert(fabsf(desc.clouds[0].origin[0] - 70.0f / 4000.0f) < 1e-6f);
    test_int(frame_renders(world, null, NULL), 0);

    /* Measured from the last render, not from the start */
    cloud_offset(world, e, 120, 0);
    test_int(frame_renders(world, null, NULL), 0);
    cloud_offset(world, e, 140, 0);
    test_int(frame_renders(world, null, NULL), 1);

    /* Along Z as well */
    cloud_offset(world, e, 140, 70);
    test_int(frame_renders(world, null, NULL), 1);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_fini(world);
}

void CloudEnvironment_move_with_wrap(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    box_render_atmosphere_env_t desc;
    cloud_offset(world, e, 3980, 0);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].offset[0], 3980.0f);
    test_int(frame_renders(world, null, NULL), 0);

    /* 3980 to 4010 wraps to 10: 30 units, not 3970 */
    cloud_offset(world, e, 4010, 0);
    test_int(frame_renders(world, null, NULL), 0);

    /* A negative offset wraps to the end of the size */
    cloud_offset(world, e, -10, 0);
    test_int(frame_renders(world, null, NULL), 0);

    /* Past 1/64 of the size across the wrap */
    cloud_offset(world, e, 50, 0);
    test_int(frame_renders(world, null, &desc), 1);
    test_flt(desc.clouds[0].offset[0], 50.0f);

    /* A whole size later is the same pattern */
    cloud_offset(world, e, 4050, 0);
    test_int(frame_renders(world, null, NULL), 0);

    ecs_fini(world);
}

void CloudEnvironment_small_move_does_not_render(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    test_int(frame_renders(world, null, NULL), 1);
    int32_t version = version_of(world, scene);
    int32_t i;

    /* Steps that add up to 50 units since the render, below 1/64 of the size of 4000 */
    for (i = 1; i <= 30; i ++) {
        cloud_offset(world, e, (float)i * 1.5f, (float)i * 0.75f);
        test_int(frame_renders(world, null, NULL), 0);
    }

    test_int(version_of(world, scene), version);
    ecs_fini(world);
}

void CloudEnvironment_velocity_renders_periodically(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    ecs_get_mut(world, e, CloudLayer)->velocity[0] = 50;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 1);
    int32_t i, count = 0, last = -100;

    /* 62.5 units at 50 units per second render every 1.25 s, 75 frames: in 4 s at most 4 */
    for (i = 0; i < 240; i ++) {
        int32_t n = frame_renders(world, null, NULL);
        test_assert(n <= 1);
        if (n) {
            test_assert(i - last >= 70);
            last = i;
            count ++;
        }
    }

    test_assert(count >= 3);
    test_assert(count <= 4);
    ecs_fini(world);
}

void CloudEnvironment_moving_clouds_keep_reflection_probes(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_add(world, scene, ReflectionBake);
    ReflectionBake *bake = ecs_get_mut(world, scene, ReflectionBake);
    bake->scene_probe = true;
    bake->environment = scene;
    ecs_modified(world, scene, ReflectionBake);
    ecs_entity_t probe = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, probe, Position, {0, 1, 0});
    ecs_add(world, probe, ReflectionProbe);
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    test_int(frame_renders(world, null, NULL), 1);
    frame(world);
    box_render_reflect_settle(world, scene);
    frame(world);
    test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
    int32_t version = version_of(world, scene);

    /* The environment renders the moved clouds, the probes keep their captures */
    cloud_offset(world, e, 200, 0);
    test_int(frame_renders(world, null, NULL), 1);
    test_int(version_of(world, scene), version + 1);
    test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
    frame(world);
    test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);

    /* So does wind */
    ecs_get_mut(world, e, CloudLayer)->velocity[0] = 400;
    ecs_modified(world, e, CloudLayer);
    int32_t i, count = 0;
    for (i = 0; i < 60; i ++) {
        count += frame_renders(world, null, NULL);
        test_int(box_render_reflect_queue(world, scene, NULL, 0), 0);
    }

    test_assert(count >= 3);

    /* A changed layer captures them again */
    CloudLayer *c = ecs_get_mut(world, e, CloudLayer);
    c->velocity[0] = 0;
    c->density = 0.5f;
    ecs_modified(world, e, CloudLayer);
    test_int(frame_renders(world, null, NULL), 1);
    int32_t queued = box_render_reflect_queue(world, scene, NULL, 0);
    if (!queued) {
        frame(world);
        queued = box_render_reflect_queue(world, scene, NULL, 0);
    }

    test_assert(queued > 0);

    ecs_fini(world);
}

void CloudEnvironment_moving_clouds_keep_probe_volume(void) {
    // atmosphere/clouds
    box_render_null_t *null;
    ecs_entity_t scene;
    ecs_world_t *world = env_world(&null, &scene);
    ecs_set(world, scene, LightBake, {
        .probe_spacing = 2, .bounces = 1, .capture_size = 8, .budget_ms = 1,
        .bounds_padding = 1, .environment = scene, .compute = true
    });
    ecs_entity_t shape = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, shape, Box, {1, 1, 1});
    ecs_set(world, shape, Position, {0, 0, 0});
    ecs_entity_t tex = cloud_texture(world, null);
    ecs_entity_t e = cloud_layer(world, scene, tex, 1500);
    test_int(frame_renders(world, null, NULL), 1);
    int32_t i;
    for (i = 0; i < 3; i ++) {
        frame(world);
    }

    test_assert(!volume_fill(null));

    /* The environment renders the moved clouds, the volume is not filled again */
    cloud_offset(world, e, 200, 0);
    test_int(frame_renders(world, null, NULL), 1);
    test_assert(!volume_fill(null));
    frame(world);
    test_assert(!volume_fill(null));

    /* A changed layer fills it again */
    ecs_get_mut(world, e, CloudLayer)->density = 0.5f;
    ecs_modified(world, e, CloudLayer);
    bool filled = false;
    for (i = 0; i < 2; i ++) {
        frame(world);
        filled |= volume_fill(null);
    }

    test_assert(filled);
    ecs_fini(world);
}
