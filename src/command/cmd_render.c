/**
 * @file command/cmd_render.c
 * @brief Render commands: capture cameras and render entities, prefabs or templates on their own.
 *
 * Both commands create an empty PNG resource in the shell session and return it right away.
 * The renderer replaces its file with the image in one rename once the GPU finished the frame
 * that holds it, at the latest on box_render_sync, so a client reads an empty or whole image.
 *
 * A capture shows a camera as its view renders it, effects included. A render shows its
 * subject alone in a private scene, plain lit without shadows or effects, so the image does
 * not depend on the views of the world.
 */

#include "command.h"
#include "../render/render.h"
#include <float.h>
#include <math.h>
#include <string.h>

/** Width and height in pixels of the image of a render */
#define BOX_CMD_RENDER_SIZE (512)

/** Vertical field of view in degrees of the camera of a render */
#define BOX_CMD_RENDER_FOV (45.0f)

/** Smallest radius a subject is framed with, in world units */
#define BOX_CMD_RENDER_RADIUS (0.5f)

/** Distance of the camera of a render as a multiple of the distance that fits the subject */
#define BOX_CMD_RENDER_MARGIN (1.1f)

/** A render: the private scene of its subject and the image it writes */
typedef struct box_cmd_render_job_t {
    ecs_entity_t root;      /**< Scene root that holds the copy of the subject */
    ecs_entity_t subject;   /**< The instance or copy of the subject */
    char *path;             /**< PNG file of the render */
    bool requested;         /**< The capture was requested, the scene is deleted next frame */
} box_cmd_render_job_t;

/** State of the render commands of a world */
typedef struct box_cmd_render_t {
    ecs_vec_t jobs;         /**< box_cmd_render_job_t */
    char *tmp_dir;          /**< Temporary folder of the process the images go in, NULL when
                             *   none was opened */
    bool tmp_opened;        /**< The temporary folder was opened, or failed to open */
} box_cmd_render_t;

/** Free the state of the render commands.
 *
 * @param ctx The state
 */
static void box_cmd_render_free(
    void *ctx)
{
    box_cmd_render_t *state = ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->jobs); i ++) {
        ecs_os_free(ecs_vec_get_t(&state->jobs, box_cmd_render_job_t, i)->path);
    }

    ecs_vec_fini_t(NULL, &state->jobs, box_cmd_render_job_t);
    ecs_os_free(state->tmp_dir);
    ecs_os_free(state);
}

#ifndef __EMSCRIPTEN__

/** Give a session without resources its resource folder in the temporary folder of the process,
 * shell/<session> in /tmp/box/<pid>.
 *
 * flecs puts the folder in .flecs/shell/<session> of the working directory and deletes it with
 * the session. Session ids repeat between processes, so processes in one directory would share
 * the folder, and the first to close its session would delete the images of the others.
 *
 * Without a temporary folder the session keeps the folder of flecs.
 *
 * @param world The world
 * @param state The state of the render commands
 * @param session The session
 */
static void box_cmd_render_folder(
    ecs_world_t *world,
    box_cmd_render_t *state,
    ecs_entity_t session)
{
    EcsCommandSession *s = ecs_get_mut(world, session, EcsCommandSession);
    char *shell = NULL, *path = NULL;
    if (!s || s->resource_path) {
        goto done;
    }

    if (!state->tmp_opened) {
        state->tmp_dir = box_app_tmp_open(world);
        state->tmp_opened = true;
    }

    if (!state->tmp_dir) {
        goto done;
    }

    shell = flecs_asprintf("%s/shell", state->tmp_dir);
    path = flecs_asprintf("%s/%llu", shell, (unsigned long long)session);
    if (ecs_os_mkdir(shell) || ecs_os_mkdir(path)) {
        ecs_err("cannot create the resource folder '%s'", path);
        goto done;
    }

    s->resource_path = path;
    path = NULL;
done:
    ecs_os_free(shell);
    ecs_os_free(path);
}

#endif

/** Create the PNG resource of an image in the session of the command and get its file.
 *
 * @param ctx The call context
 * @param resource Receives the resource
 * @return The file of the resource, NULL in the browser, which writes no captures, or when the
 * command runs outside of a session; the caller frees it
 */
static char* box_cmd_render_file(
    const ecs_function_ctx_t *ctx,
    ecs_shell_resource_t *resource)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = ecs_shell_get_session(world);
    char *path = NULL;
#ifdef __EMSCRIPTEN__
    (void)session;
    (void)resource;
    ecs_err("captures are not available in the browser");
    goto done;
#else
    if (!session) {
        ecs_err("images are only written by commands that run in a shell session");
        goto done;
    }

    box_cmd_render_folder(world, ctx->ctx, session);
    if (ecs_shell_resource_create(world, session, "image/png", NULL, 0, resource)) {
        goto done;
    }

    path = box_command_resource_file(world, session, resource->name);
#endif
done:
    return path;
}

/** Find the surface that is shown first: the surface created first.
 *
 * @param world The world
 * @return The surface entity, 0 when there is none
 */
static ecs_entity_t box_cmd_render_surface(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, Surface);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (!result || it.entities[i] < result) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

/** Get the size of the images of the capture command: the size of the surface shown first.
 *
 * @param world The world
 * @param width Receives the width in pixels
 * @param height Receives the height in pixels
 */
void box_cmd_render_size(
    ecs_world_t *world,
    int32_t *width,
    int32_t *height)
{
    ecs_entity_t surface = box_cmd_render_surface(world);
    const SurfaceStatus *status = surface ? ecs_get(world, surface, SurfaceStatus) : NULL;
    const Surface *desc = surface ? ecs_get(world, surface, Surface) : NULL;
    width[0] = BOX_SURFACE_WIDTH;
    height[0] = BOX_SURFACE_HEIGHT;
    if (status && status->width > 0 && status->height > 0) {
        width[0] = status->width;
        height[0] = status->height;
    } else if (desc && desc->width > 0 && desc->height > 0) {
        width[0] = desc->width;
        height[0] = desc->height;
    }
}

/** Find the first view, in render order (View.order, then creation), that renders a camera.
 *
 * @param world The world
 * @param camera The camera, 0 for any camera with a Camera
 * @param root Parent the view must have, 0 for any parent
 * @param clearing Only consider views that clear
 * @return The view entity, 0 when there is none
 */
static ecs_entity_t box_cmd_render_view(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t root,
    bool clearing)
{
    ecs_entity_t result = 0;
    int32_t order = 0;
    ecs_query_t *q = ecs_query(world, { .terms = {{ .id = ecs_id(View) }} });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t c = views[i].camera;
            bool match = camera ? c == camera
                : c && ecs_is_alive(world, c) && ecs_has(world, c, Camera);
            match = match && (!clearing || views[i].clear.enabled);
            match = match && (!root || ecs_has_pair(world, it.entities[i], EcsChildOf, root));
            if (match && (!result ||
                box_view_compare(views[i].order, it.entities[i], order, result) < 0))
            {
                result = it.entities[i];
                order = views[i].order;
            }
        }
    }

    ecs_query_fini(q);

    return result;
}

/** Get the parent of the views the surface shown first renders.
 *
 * @param world The world
 * @return The view root of the surface, 0 for every view
 */
static ecs_entity_t box_cmd_render_view_root(
    ecs_world_t *world)
{
    ecs_entity_t surface = box_cmd_render_surface(world);
    const Surface *s = surface ? ecs_get(world, surface, Surface) : NULL;

    return s ? s->view_root : 0;
}

/** Get the camera of a command: the camera argument, else the camera of the first view of the
 * surface shown first. Logs an error when the argument is not a camera or there is no camera.
 *
 * @param world The world
 * @param argc Number of arguments
 * @param argv The camera, omitted for the active camera
 * @return The camera, 0 on an error
 */
ecs_entity_t box_cmd_render_camera(
    ecs_world_t *world,
    int32_t argc,
    const ecs_value_t *argv)
{
    ecs_entity_t result = argc > 0 ? *(ecs_entity_t*)argv[0].ptr : 0;
    if (argc > 0 && (!result || !ecs_is_alive(world, result) || !ecs_has(world, result, Camera))) {
        char *path = ecs_get_path(world, result);
        ecs_err("'%s' is not a camera", path);
        ecs_os_free(path);
        result = 0;
    } else if (!result) {
        ecs_entity_t view = box_cmd_render_view(world, 0, box_cmd_render_view_root(world), false);
        if (view) {
            result = ecs_get(world, view, View)->camera;
        } else {
            ecs_err("no active camera");
        }
    }

    return result;
}

/** Get the clear of a capture: of the first view of the camera when it clears, else of the
 * first view that clears.
 *
 * @param world The world
 * @param camera The camera
 * @param out Receives the clear settings, not enabled when no view clears
 */
static void box_cmd_render_clear(
    ecs_world_t *world,
    ecs_entity_t camera,
    ClearSettings *out)
{
    ecs_entity_t view = box_cmd_render_view(world, camera, 0, true);
    ecs_os_memset_t(out, 0, ClearSettings);
    if (!view) {
        view = box_cmd_render_view(world, 0, box_cmd_render_view_root(world), true);
    }

    if (view) {
        out[0] = ecs_get(world, view, View)->clear;
    }
}

/** The capture command: write a PNG of the active camera or of a camera.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The camera, omitted for the active camera
 * @param result Receives the PNG resource
 */
static void box_cmd_capture(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t camera = box_cmd_render_camera(world, argc, argv);
    box_render_capture_desc_t desc = {0};
    char *path = NULL;
    if (!camera) {
        goto done;
    }

    path = box_cmd_render_file(ctx, result->ptr);
    if (!path) {
        goto done;
    }

    desc.camera = camera;
    desc.path = path;
    box_cmd_render_size(world, &desc.width, &desc.height);
    box_cmd_render_clear(world, camera, &desc.clear);
    box_render_capture(world, &desc);
done:
    ecs_os_free(path);
}

/** Check whether an entity belongs to the flecs scope.
 *
 * @param world The world
 * @param e The entity
 * @return Whether the entity or one of its parents is the flecs module
 */
static bool box_cmd_render_builtin(
    ecs_world_t *world,
    ecs_entity_t e)
{
    while (e && e != EcsFlecs) {
        e = ecs_get_parent(world, e);
    }

    return e == EcsFlecs;
}

/** Check whether an entity is a script template.
 *
 * @param world The world
 * @param e The entity
 * @return Whether adding the entity as a component instantiates a template
 */
static bool box_cmd_render_is_template(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsScript *script = ecs_get(world, e, EcsScript);

    return script && script->template_;
}

/** Check whether a component of an entity is copied into the scene of a render.
 *
 * Reflected components and tags are copied; engine state without reflection, the builtin
 * components and the components that would show the copy elsewhere are not.
 *
 * @param world The world
 * @param id The component
 * @return Whether the component is copied
 */
static bool box_cmd_render_copies(
    ecs_world_t *world,
    ecs_id_t id)
{
    const ecs_type_info_t *ti = ecs_get_type_info(world, id);
    bool result = !ECS_IS_PAIR(id) && !box_cmd_render_builtin(world, id);
    result = result && id != ecs_id(View) && id != ecs_id(Surface) &&
        id != ecs_id(EcsCommandSession);

    return result && (!ti || ecs_get(world, id, EcsType));
}

/** Copy an entity and the children it does not generate into the scene of a render.
 *
 * Children of an entity that is a prefab instance or a template instance are created again
 * by the copy, so they are not copied.
 *
 * @param world The world
 * @param src The entity
 * @param parent Parent of the copy
 * @return The copy
 */
static ecs_entity_t box_cmd_render_copy(
    ecs_world_t *world,
    ecs_entity_t src,
    ecs_entity_t parent)
{
    ecs_entity_t result = ecs_new_w_pair(world, EcsChildOf, parent);
    const ecs_type_t *type = ecs_get_type(world, src);
    bool generated = false;
    ecs_vec_t children;
    int32_t i;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        const ecs_type_info_t *ti = ecs_get_type_info(world, id);
        if (ECS_IS_PAIR(id) && ECS_PAIR_FIRST(id) == EcsIsA) {
            ecs_add_id(world, result, id);
            generated |= id != ecs_pair(EcsIsA, DynamicTransformSpread);
        } else if (box_cmd_render_copies(world, id) && ti && ti->size) {
            ecs_value_t value = ecs_value_init(world, id, ecs_get_id(world, src, id));
            ecs_set_id(world, result, id, (size_t)ti->size, value.ptr);
            ecs_value_fini(world, &value);
            generated |= box_cmd_render_is_template(world, id);
        } else if (box_cmd_render_copies(world, id)) {
            ecs_add_id(world, result, id);
        }
    }

    if (!generated) {
        ecs_iter_t it = ecs_children(world, src);
        while (ecs_children_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&children); i ++) {
        box_cmd_render_copy(world, *ecs_vec_get_t(&children, ecs_entity_t, i), result);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);

    return result;
}

/** The render command: render an entity, prefab or template on its own.
 *
 * A prefab is instantiated, a template added and an entity copied into a private scene; the
 * camera is fitted to the subject in the frame of the command and the scene is deleted in
 * the next frame.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The subject
 * @param result Receives the PNG resource
 */
static void box_cmd_render(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    box_cmd_render_t *state = ctx->ctx;
    ecs_entity_t subject = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t root = 0, instance = 0;
    char *path = NULL;
    (void)argc;
    if (!subject || !ecs_is_alive(world, subject)) {
        ecs_err("entity #%u is not alive", (uint32_t)subject);
        goto done;
    }

    if (box_cmd_render_builtin(world, subject)) {
        path = ecs_get_path(world, subject);
        ecs_err("cannot render '%s', it is in the flecs scope", path);
        goto done;
    }

    path = box_cmd_render_file(ctx, result->ptr);
    if (!path) {
        goto done;
    }

    root = ecs_new_w_pair(world, EcsChildOf, ecs_shell_get_session(world));
    ecs_set(world, root, Scene, {0});
    if (ecs_has_id(world, subject, EcsPrefab)) {
        instance = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_add_pair(world, instance, EcsIsA, subject);
    } else if (box_cmd_render_is_template(world, subject)) {
        instance = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_add_id(world, instance, subject);
    } else {
        instance = box_cmd_render_copy(world, subject, root);
    }

    box_cmd_render_job_t *job = ecs_vec_append_t(NULL, &state->jobs, box_cmd_render_job_t);
    job->root = root;
    job->subject = instance;
    job->path = path;
    job->requested = false;
    path = NULL;
done:
    ecs_os_free(path);
}

/** Collect an entity and its descendants.
 *
 * @param world The world
 * @param e The entity
 * @param out Receives the entities
 */
static void box_cmd_render_collect(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out)
{
    ecs_vec_t children;
    int32_t i;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = e;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
        }
    }

    for (i = 0; i < ecs_vec_count(&children); i ++) {
        box_cmd_render_collect(world, *ecs_vec_get_t(&children, ecs_entity_t, i), out);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

/** Get the 8 world space corners of the local bounds of the mesh of a shape, with the scale
 * of its ShapeMesh and its WorldTransform.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param e The entity
 * @param out Receives the corners
 * @return Whether the entity has a mesh in the cache
 */
bool box_cmd_render_corners(
    ecs_world_t *world,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    vec3 out[8])
{
    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    const box_mesh_t *mesh = shape && cache ? box_mesh_cache_get(cache, shape->mesh) : NULL;
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    mat4 m;
    int32_t i;
    if (!mesh) {
        goto done;
    }

    if (transform) {
        ecs_os_memcpy_t(m, transform->m, mat4);
    } else {
        glm_mat4_identity(m);
    }

    for (i = 0; i < 8; i ++) {
        vec3 local = {
            ((i & 1) ? mesh->max[0] : mesh->min[0]) * shape->scale[0],
            ((i & 2) ? mesh->max[1] : mesh->min[1]) * shape->scale[1],
            ((i & 4) ? mesh->max[2] : mesh->min[2]) * shape->scale[2]
        };

        glm_mat4_mulv3(m, local, 1.0f, out[i]);
    }

done:
    return mesh != NULL;
}

/** Grow bounds by the world space box of the mesh of a shape.
 *
 * @param world The world
 * @param cache The mesh cache
 * @param e The entity
 * @param min The minimum corner
 * @param max The maximum corner
 * @return Whether the entity has a mesh
 */
static bool box_cmd_render_grow(
    ecs_world_t *world,
    const box_mesh_cache_t *cache,
    ecs_entity_t e,
    vec3 min,
    vec3 max)
{
    vec3 corners[8];
    int32_t i;
    bool result = box_cmd_render_corners(world, cache, e, corners);
    for (i = 0; result && i < 8; i ++) {
        glm_vec3_minv(min, corners[i], min);
        glm_vec3_maxv(max, corners[i], max);
    }

    return result;
}

/** Ask for the image of a render, with a camera that frames the bounds of its subject.
 *
 * @param world The world
 * @param job The render
 */
static void box_cmd_render_request(
    ecs_world_t *world,
    const box_cmd_render_job_t *job)
{
    const box_mesh_cache_t *cache = box_mesh_cache_of(world);
    vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX }, max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    vec3 center, eye, dir = { 1.0f, 0.8f, 1.0f }, up = { 0, 1, 0 };
    bool bounds = false;
    ecs_vec_t entities;
    mat4 view;
    int32_t i;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    box_cmd_render_collect(world, job->subject, &entities);
    for (i = 0; i < ecs_vec_count(&entities); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&entities, ecs_entity_t, i);
        bounds |= box_cmd_render_grow(world, cache, e, min, max);
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
    if (!bounds) {
        const WorldTransform *t = ecs_get(world, job->subject, WorldTransform);
        glm_vec3_zero(min);
        if (t) {
            min[0] = t->m[3][0];
            min[1] = t->m[3][1];
            min[2] = t->m[3][2];
        }

        glm_vec3_copy(min, max);
    }

    glm_vec3_center(min, max, center);
    float radius = glm_vec3_distance(min, max) * 0.5f;
    radius = radius > BOX_CMD_RENDER_RADIUS ? radius : BOX_CMD_RENDER_RADIUS;
    float distance = BOX_CMD_RENDER_MARGIN * radius / sinf(glm_rad(BOX_CMD_RENDER_FOV) * 0.5f);
    glm_vec3_normalize(dir);
    glm_vec3_scale(dir, distance, eye);
    glm_vec3_add(center, eye, eye);
    glm_lookat(eye, center, up, view);

    box_render_capture_desc_t desc = {
        .camera_data = {
            .fov = BOX_CMD_RENDER_FOV,
            .near_ = (distance - radius) * 0.5f
        },
        .scene = job->root,
        .clear = { .enabled = true, .color = { 48, 52, 60, 255 } },
        .width = BOX_CMD_RENDER_SIZE,
        .height = BOX_CMD_RENDER_SIZE,
        .path = job->path
    };

    glm_mat4_inv(view, desc.transform);
    box_render_capture(world, &desc);
}

/** Request the images of new renders and delete the scenes of renders requested earlier.
 *
 * @param it The iterator of the system
 */
static void BoxRenderCommands(
    ecs_iter_t *it)
{
    box_cmd_render_t *state = it->ctx;
    ecs_world_t *world = it->world;
    int32_t i = 0;
    while (i < ecs_vec_count(&state->jobs)) {
        box_cmd_render_job_t *job = ecs_vec_get_t(&state->jobs, box_cmd_render_job_t, i);
        bool alive = ecs_is_alive(world, job->root) && ecs_is_alive(world, job->subject);
        if (alive && !job->requested) {
            box_cmd_render_request(world, job);
            job->requested = true;
            i ++;
        } else {
            if (alive) {
                ecs_delete(world, job->root);
            }

            ecs_os_free(job->path);
            ecs_vec_remove_t(&state->jobs, box_cmd_render_job_t, i);
        }
    }
}

void box_cmd_render_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxSurface);
    box_cmd_render_t *state = ecs_os_calloc_t(box_cmd_render_t);
    ecs_vec_init_t(NULL, &state->jobs, box_cmd_render_job_t, 0);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderCommands" }),
        .phase = EcsOnStore,
        .run = BoxRenderCommands,
        .ctx = state,
        .ctx_free = box_cmd_render_free,
        .immediate = true
    });

    box_command_register(world,
        "Write a PNG of the active camera or of a camera to a resource next frame",
        (ecs_function_desc_t){
            .name = "capture",
            .return_type = ecs_id(ecs_shell_resource_t),
            .params = {
                { .name = "camera", .type = ecs_id(ecs_entity_t), .optional = true,
                  .brief = "Camera to capture (default: the active camera)" }
            },
            .callback = box_cmd_capture,
            .ctx = state
        });

    box_command_register(world,
        "Write a PNG of an entity, prefab or template on its own to a resource next frame",
        (ecs_function_desc_t){
            .name = "render",
            .return_type = ecs_id(ecs_shell_resource_t),
            .params = {
                { .name = "subject", .type = ecs_id(ecs_entity_t),
                  .brief = "Entity, prefab or template to render" }
            },
            .callback = box_cmd_render,
            .ctx = state
        });
}
