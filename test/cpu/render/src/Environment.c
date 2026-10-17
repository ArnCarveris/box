/**
 * @file Environment.c
 * @brief Tests for the environment sources, their status, the environment of the views and the
 * cubemaps and images the packets carry.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "environment.h"

/** Create a world that renders into a null backend.
 *
 * @param null Receives the backend
 * @return The world
 */
static ecs_world_t* world_new(
    box_render_null_t **null)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);

    return world;
}

/** Create a lit view with a camera that is lit with an environment source.
 *
 * @param world The world
 * @param source The source, 0 for none
 * @return The view
 */
static ecs_entity_t view_new(
    ecs_world_t *world,
    ecs_entity_t source)
{
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->lighting.environment.source = source;
    ecs_modified(world, view, View);

    return view;
}

/** Run a frame and wait until the backend rendered it.
 *
 * @param world The world
 */
static void frame(
    ecs_world_t *world)
{
    ecs_progress(world, 0);
    box_render_sync(world);
}

/** Find the view of the last packet that renders a view entity.
 *
 * @param null The backend
 * @param view The view entity
 * @return The view of the packet
 */
static const box_render_view_t* packet_view(
    box_render_null_t *null,
    ecs_entity_t view)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, i);
        if (v->view == view) {
            result = v;
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Count the resource changes of a kind in the last packet.
 *
 * @param null The backend
 * @param kind The kind of resource
 * @param op What happens to it
 * @param handle Receives the handle of the last one, may be NULL
 * @return The changes
 */
static int32_t resources(
    box_render_null_t *null,
    box_render_resource_kind_t kind,
    box_render_resource_op_t op,
    uint32_t *handle)
{
    int32_t i, result = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == kind && r->op == op) {
            result ++;
            if (handle) {
                *handle = r->handle;
            }
        }
    }

    return result;
}

/** Get the description of the last cubemap create of the last packet.
 *
 * @param null The backend
 * @return The description
 */
static const box_render_environment_desc_t* created(
    box_render_null_t *null)
{
    const box_render_environment_desc_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_ENVIRONMENT && r->op == BOX_RENDER_CREATE) {
            result = box_render_packet_data(&null->last, r->data);
        }
    }

    test_assert(result != NULL);

    return result;
}

/** Run frames until the status of a source has content or an error.
 *
 * @param world The world
 * @param e The source
 * @return The status
 */
static const EnvironmentStatus* wait_status(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EnvironmentStatus *result = NULL;
    int32_t i;
    for (i = 0; i < 500; i ++) {
        frame(world);
        result = ecs_get(world, e, EnvironmentStatus);
        test_assert(result != NULL);
        if (result->version || result->error) {
            break;
        }

        ecs_os_sleep(0, 1000 * 1000);
    }

    return result;
}

void Environment_script_defaults(void) {
    // environment/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "defaults",
        "using box.light\n"
        "env { Environment: {} }\n"
        "custom { Environment: {resolution: 64, filter: Box} }\n"
        "zero { Environment: {resolution: 0, filter: Ggx} }\n", NULL) == 0);

    const Environment *env = ecs_get(world, ecs_lookup(world, "env"), Environment);
    test_assert(env != NULL);
    test_int(env->resolution, 256);
    test_int(env->filter, EnvironmentFilterGgx);
    env = ecs_get(world, ecs_lookup(world, "custom"), Environment);
    test_int(env->resolution, 64);
    test_int(env->filter, EnvironmentFilterBox);
    env = ecs_get(world, ecs_lookup(world, "zero"), Environment);
    test_int(env->resolution, 0);
    test_int(EnvironmentFilterGgx, 0);
    test_int(EnvironmentFilterBox, 1);

    const EnvironmentStatus *status = ecs_get(
        world, ecs_lookup(world, "env"), EnvironmentStatus);
    test_assert(status != NULL);
    test_int(status->version, 0);
    test_assert(status->error == NULL);

    ecs_fini(world);
}

void Environment_c_defaults(void) {
    // environment/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Environment);
    const Environment *env = ecs_get(world, e, Environment);
    test_int(env->resolution, 256);
    test_int(env->filter, EnvironmentFilterGgx);
    test_assert(ecs_has(world, e, EnvironmentStatus));
    ecs_remove(world, e, Environment);
    test_assert(!ecs_has(world, e, EnvironmentStatus));

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, Environment, {0});
    test_int(ecs_get(world, literal, Environment)->resolution, 0);
    test_assert(ecs_has(world, literal, EnvironmentStatus));

    ecs_entity_t image = ecs_new(world);
    ecs_set(world, image, EnvironmentImage, {.path = "etc/environments/axes.hdr"});
    test_assert(ecs_has(world, image, Environment));
    test_assert(ecs_has(world, image, EnvironmentStatus));
    test_int(ecs_get(world, image, Environment)->resolution, 256);
    test_str(ecs_get(world, image, EnvironmentImage)->path, "etc/environments/axes.hdr");

    ecs_fini(world);
}

void Environment_invalid_resolution_sets_error(void) {
    // environment/settings
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Environment, {.resolution = 100});
    ecs_entity_t view = view_new(world, source);
    frame(world);
    const EnvironmentStatus *status = ecs_get(world, source, EnvironmentStatus);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "power of two") != NULL);
    test_uint(packet_view(null, view)->environment.handle, 0);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);

    int32_t invalid[] = {4, 2048, 0, -8};
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_set(world, source, Environment, {.resolution = invalid[i]});
        frame(world);
        test_assert(ecs_get(world, source, EnvironmentStatus)->error != NULL);
        test_uint(packet_view(null, view)->environment.handle, 0);
    }

    ecs_set(world, source, Environment, {.resolution = 8});
    frame(world);
    test_assert(ecs_get(world, source, EnvironmentStatus)->error == NULL);
    test_assert(packet_view(null, view)->environment.handle != 0);
    test_int(created(null)->resolution, 8);

    ecs_set(world, source, Environment, {.resolution = 1024});
    frame(world);
    test_assert(ecs_get(world, source, EnvironmentStatus)->error == NULL);
    test_int(created(null)->resolution, 1024);

    ecs_set(world, source, Environment, {.resolution = 12});
    frame(world);
    test_assert(ecs_get(world, source, EnvironmentStatus)->error != NULL);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_DESTROY, NULL), 1);
    test_uint(packet_view(null, view)->environment.handle, 0);

    ecs_fini(world);
}

void Environment_view_settings_defaults(void) {
    // environment/view
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "views",
        "using box.view\n"
        "using box.light\n"
        "sky { Environment: {} }\n"
        "view { View: {} }\n"
        "custom { View: {lighting: {enabled: true, environment: {source: sky, intensity: 2, "
        "yaw: 0.5, background: true}}} }\n", NULL) == 0);

    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_uint(view->lighting.environment.source, 0);
    test_flt(view->lighting.environment.intensity, 1);
    test_flt(view->lighting.environment.yaw, 0);
    test_bool(view->lighting.environment.background, false);

    view = ecs_get(world, ecs_lookup(world, "custom"), View);
    test_uint(view->lighting.environment.source, ecs_lookup(world, "sky"));
    test_flt(view->lighting.environment.intensity, 2);
    test_flt(view->lighting.environment.yaw, 0.5f);
    test_bool(view->lighting.environment.background, true);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {.lighting.enabled = true});
    view = ecs_get(world, literal, View);
    test_flt(view->lighting.environment.intensity, 0);
    test_bool(view->lighting.environment.background, false);

    ecs_entity_t added = ecs_new(world);
    ecs_add(world, added, View);
    test_flt(ecs_get(world, added, View)->lighting.environment.intensity, 1);

    ecs_fini(world);
}

void Environment_view_without_source_uses_none(void) {
    // environment/view
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_add(world, source, Environment);
    ecs_entity_t view = view_new(world, 0);
    ecs_entity_t other = view_new(world, ecs_new(world));
    frame(world);
    test_uint(packet_view(null, view)->environment.handle, 0);
    test_uint(packet_view(null, other)->environment.handle, 0);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);

    View *v = ecs_get_mut(world, view, View);
    v->lighting.environment.source = source;
    v->lighting.environment.intensity = 3;
    v->lighting.environment.yaw = 1.5f;
    ecs_modified(world, view, View);
    frame(world);
    const box_render_view_t *pv = packet_view(null, view);
    test_assert(pv->environment.handle != 0);
    test_flt(pv->environment.intensity, 3);
    test_flt(pv->environment.yaw, 1.5f);
    test_bool(pv->environment.background, false);

    ecs_fini(world);
}

void Environment_unlit_view_uses_none(void) {
    // environment/view
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_add(world, source, Environment);
    ecs_entity_t view = view_new(world, source);
    ecs_get_mut(world, view, View)->lighting.enabled = false;
    ecs_modified(world, view, View);
    frame(world);
    test_uint(packet_view(null, view)->environment.handle, 0);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);

    ecs_get_mut(world, view, View)->lighting.enabled = true;
    ecs_modified(world, view, View);
    frame(world);
    test_assert(packet_view(null, view)->environment.handle != 0);

    ecs_fini(world);
}

void Environment_shared_source_creates_one_cubemap(void) {
    // environment/cubemap
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Environment, {.resolution = 32, .filter = EnvironmentFilterBox});
    ecs_entity_t a = view_new(world, source);
    ecs_entity_t b = view_new(world, source);
    frame(world);
    uint32_t handle = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, &handle), 1);
    test_assert(handle != 0);
    test_uint(packet_view(null, a)->environment.handle, handle);
    test_uint(packet_view(null, b)->environment.handle, handle);
    test_int(created(null)->resolution, 32);
    test_int(created(null)->filter, EnvironmentFilterBox);
    test_uint(box_render_environment_find(world, source), handle);

    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);
    test_uint(packet_view(null, a)->environment.handle, handle);
    test_uint(packet_view(null, b)->environment.handle, handle);

    ecs_entity_t other = ecs_new(world);
    ecs_add(world, other, Environment);
    ecs_entity_t c = view_new(world, other);
    frame(world);
    uint32_t other_handle = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, &other_handle), 1);
    test_assert(other_handle != handle);
    test_uint(packet_view(null, c)->environment.handle, other_handle);

    ecs_fini(world);
}

void Environment_resolution_change_recreates_cubemap(void) {
    // environment/cubemap
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, Environment, {.resolution = 64});
    ecs_entity_t view = view_new(world, source);
    frame(world);
    uint32_t handle = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, &handle), 1);

    ecs_set(world, source, Environment, {.resolution = 128});
    frame(world);
    uint32_t again = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, &again), 1);
    test_uint(again, handle);
    test_int(created(null)->resolution, 128);
    test_uint(packet_view(null, view)->environment.handle, handle);

    ecs_set(world, source, Environment, {.resolution = 128, .filter = EnvironmentFilterBox});
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_UPDATE, NULL), 1);

    ecs_fini(world);
}

void Environment_removing_source_destroys_cubemap(void) {
    // environment/cubemap
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_add(world, source, Environment);
    ecs_entity_t view = view_new(world, source);
    frame(world);
    uint32_t handle = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, &handle), 1);

    ecs_remove(world, source, Environment);
    frame(world);
    uint32_t destroyed = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_DESTROY, &destroyed), 1);
    test_uint(destroyed, handle);
    test_uint(packet_view(null, view)->environment.handle, 0);
    test_uint(box_render_environment_find(world, source), 0);

    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_DESTROY, NULL), 0);

    ecs_add(world, source, Environment);
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 1);
    ecs_delete(world, source);
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_DESTROY, NULL), 1);
    test_uint(packet_view(null, view)->environment.handle, 0);

    ecs_fini(world);
}

void Environment_image_sent_once_per_version(void) {
    // environment/image
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/axes.hdr"});
    const EnvironmentStatus *status = wait_status(world, source);
    test_assert(status->error == NULL);
    test_int(status->version, 1);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 1);
    uint32_t handle = 0;
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, &handle), 1);
    test_uint(handle, box_render_environment_find(world, source));
    test_int(box_render_environment_content(world, source), 1);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind == BOX_RENDER_ENVIRONMENT_IMAGE) {
            const box_render_environment_image_t *img = box_render_packet_data(
                &null->last, r->data);
            test_int(img->width, 64);
            test_int(img->height, 32);
            test_int(img->version, 1);
            test_int(r->size, sizeof(box_render_environment_image_t) + 64 * 32 * 8);
        }
    }

    for (i = 0; i < 3; i ++) {
        frame(world);
        test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 0);
        test_int(ecs_get(world, source, EnvironmentStatus)->version, 1);
    }

    ecs_set(world, source, Environment, {.resolution = 64});
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 1);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 1);
    test_int(ecs_get(world, source, EnvironmentStatus)->version, 2);
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 0);

    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/sky.hdr"});
    for (i = 0; i < 500 && ecs_get(world, source, EnvironmentStatus)->version == 2; i ++) {
        frame(world);
        ecs_os_sleep(0, 1000 * 1000);
    }

    test_int(ecs_get(world, source, EnvironmentStatus)->version, 3);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 1);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT, BOX_RENDER_CREATE, NULL), 0);
    frame(world);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 0);

    ecs_fini(world);
}

void Environment_missing_file_sets_error(void) {
    // environment/image
    box_render_null_t *null;
    ecs_world_t *world = world_new(&null);

    ecs_log_set_level(-4);
    ecs_entity_t source = ecs_new(world);
    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/missing.hdr"});
    const EnvironmentStatus *status = wait_status(world, source);
    test_assert(status->error != NULL);
    test_assert(strstr(status->error, "missing.hdr") != NULL);
    test_int(status->version, 0);
    test_int(resources(null, BOX_RENDER_ENVIRONMENT_IMAGE, BOX_RENDER_UPDATE, NULL), 0);

    ecs_set(world, source, Environment, {.resolution = 3});
    frame(world);
    test_assert(strstr(ecs_get(world, source, EnvironmentStatus)->error, "power of two"));
    ecs_set(world, source, Environment, {.resolution = 16});
    frame(world);
    test_assert(strstr(ecs_get(world, source, EnvironmentStatus)->error, "missing.hdr"));

    ecs_set(world, source, EnvironmentImage, {.path = "etc/environments/axes.hdr"});
    int32_t i;
    for (i = 0; i < 500 && !ecs_get(world, source, EnvironmentStatus)->version; i ++) {
        frame(world);
        ecs_os_sleep(0, 1000 * 1000);
    }

    status = ecs_get(world, source, EnvironmentStatus);
    test_int(status->version, 1);
    test_assert(status->error == NULL);

    ecs_fini(world);
}
