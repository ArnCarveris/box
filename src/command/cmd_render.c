/**
 * @file command/cmd_render.c
 * @brief Render commands: capture cameras and render entities, prefabs or templates on their own.
 *
 * Both commands write PNG files into the resources folder of the session and return their
 * names right away; the renderer writes the files once the frame that holds them rendered.
 *
 * A capture shows a camera as its view renders it, effects included. A render shows its
 * subject alone in a private scene, plain lit without shadows or effects, so the image does
 * not depend on the views of the world.
 */

#include "command.h"
#include "../render/render.h"
#include "../geometry/mesh_cache.h"
#include <float.h>
#include <math.h>
#include <string.h>

/** Marker after the name of an image that the renderer has not written yet */
#define BOX_CMD_RENDER_PENDING " (pending)"

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
    int32_t captures;       /**< Images the capture command named */
    int32_t renders;        /**< Images the render command named */
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
    ecs_os_free(state);
}

/** Name the next image of a session and get its path in the resources folder.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param session The session entity
 * @param prefix Start of the file name
 * @param counter Counter of the images with this prefix
 * @param name Receives the file name, which the caller frees
 * @return The path, NULL when the session has no folder, keeps no images or keeps as many
 * images as it may that are still being written; the caller frees it
 */
static char* box_cmd_render_file(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *prefix,
    int32_t *counter,
    char **name)
{
    const TerminalSession *s = ecs_get(ctx->world, session, TerminalSession);
    char *path = NULL;
    int kept;
    name[0] = NULL;
    if (!s || !s->dir) {
        box_command_error(ctx, result, "the session has no folder");
        goto done;
    }

    name[0] = flecs_asprintf("%s_%d.png", prefix, counter[0] ++);
    path = flecs_asprintf("%s/%s/%s", s->dir, BOX_TERMINAL_RESOURCES, name[0]);
    kept = box_terminal_image(ctx->world, session, path);
    if (kept == BOX_TERMINAL_IMAGE_NONE) {
        box_command_error(ctx, result, "the session keeps no images, TerminalLimits.images is 0");
    } else if (kept == BOX_TERMINAL_IMAGE_PENDING) {
        box_command_error(ctx, result, "%d images are still being written, "
            "TerminalLimits.images is %d", s->images_max, s->images_max);
    }

    if (kept) {
        ecs_os_free(path);
        ecs_os_free(name[0]);
        path = name[0] = NULL;
    }

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
static void box_cmd_render_size(
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

/** Find the first view, in creation order, that renders a camera.
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
            if (match && (!result || it.entities[i] < result)) {
                result = it.entities[i];
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

/** Resolve the camera of a capture, failing the command when the entity has no Camera.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param session The session entity
 * @param path Path of the camera
 * @return The camera, 0 when the command failed
 */
static ecs_entity_t box_cmd_render_camera(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *path)
{
    ecs_entity_t e = 0;
    if (box_shell_resolve(ctx, result, session, path, BoxShellRead, &e)) {
        e = 0;
    } else if (!ecs_has(ctx->world, e, Camera)) {
        box_command_error(ctx, result, "'%s' is not a camera", path);
        e = 0;
    }

    return e;
}

/** Resolve the cameras of a capture line, separated by commas, spaces or brackets.
 *
 * @param ctx The call context
 * @param result The result, which gets the error
 * @param session The session entity
 * @param arg The camera paths, NULL or empty for the active camera
 * @param out Receives the cameras
 * @return 0 on success, -1 when the command failed
 */
static int box_cmd_render_cameras(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *arg,
    ecs_vec_t *out)
{
    ecs_world_t *world = ctx->world;
    char *copy = ecs_os_strdup(arg ? arg : "");
    char *token = strtok(copy, ", \t[]");
    ecs_entity_t camera = 0;
    int ret = -1;
    while (token) {
        camera = box_cmd_render_camera(ctx, result, session, token);
        if (!camera) {
            goto done;
        }

        ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = camera;
        token = strtok(NULL, ", \t[]");
    }

    if (!ecs_vec_count(out)) {
        ecs_entity_t view = box_cmd_render_view(world, 0, box_cmd_render_view_root(world), false);
        if (!view) {
            box_command_error(ctx, result, "no active camera");
            goto done;
        }

        ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = ecs_get(world, view, View)->camera;
    }

    ret = 0;
done:
    ecs_os_free(copy);

    return ret;
}

/** The capture command: write a PNG of the active camera or of each named camera.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the camera paths
 * @param result Receives the names of the images in the resources folder
 */
static void box_cmd_capture(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    box_cmd_render_t *state = ctx->ctx;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *arg = argc > 1 ? *(char**)argv[1].ptr : NULL;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t cameras;
    int32_t i, width, height;
    ecs_vec_init_t(NULL, &cameras, ecs_entity_t, 0);
    if (box_cmd_render_cameras(ctx, result, session, arg, &cameras)) {
        goto done;
    }

    box_cmd_render_size(world, &width, &height);
    for (i = 0; i < ecs_vec_count(&cameras); i ++) {
        box_render_capture_desc_t desc = {
            .camera = *ecs_vec_get_t(&cameras, ecs_entity_t, i),
            .width = width,
            .height = height
        };

        char *name = NULL;
        char *path = box_cmd_render_file(ctx, result, session, "capture", &state->captures, &name);
        if (!path) {
            goto done;
        }

        box_cmd_render_clear(world, desc.camera, &desc.clear);
        desc.path = path;
        box_render_capture(world, &desc);
        ecs_strbuf_append(&buf, "%s%s%s", i ? "\n" : "", name, BOX_CMD_RENDER_PENDING);
        ecs_os_free(name);
        ecs_os_free(path);
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    ecs_strbuf_reset(&buf);
    ecs_vec_fini_t(NULL, &cameras, ecs_entity_t);
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
    result = result && id != ecs_id(View) && id != ecs_id(Surface) && id != ecs_id(Terminal);

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
            generated = true;
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
 * @param argv The session and the subject
 * @param result Receives the name of the image in the resources folder
 */
static void box_cmd_render(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    box_cmd_render_t *state = ctx->ctx;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *arg = *(char**)argv[1].ptr;
    ecs_entity_t subject = 0, root = 0, instance = 0;
    char *name = NULL, *path = NULL;
    (void)argc;
    if (box_shell_resolve(ctx, result, session, arg, BoxShellWrite, &subject)) {
        goto done;
    }

    path = box_cmd_render_file(ctx, result, session, "render", &state->renders, &name);
    if (!path) {
        goto done;
    }

    root = ecs_new_w_pair(world, EcsChildOf, session);
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
    *(char**)result->ptr = flecs_asprintf("%s%s", name, BOX_CMD_RENDER_PENDING);
done:
    ecs_os_free(name);
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

        vec3 world_pos;
        glm_mat4_mulv3(m, local, 1.0f, world_pos);
        glm_vec3_minv(min, world_pos, min);
        glm_vec3_maxv(max, world_pos, max);
    }

done:
    return mesh != NULL;
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

    box_command_register(world, &(box_command_desc_t){
        .name = "capture",
        .brief = "PNG of the active camera or of cameras a, b in resources, written next frame",
        .params = {{
            .name = "cameras", .type = ecs_id(ecs_string_t), .optional = true, .rest = true
        }},
        .callback = box_cmd_capture,
        .ctx = state
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "render",
        .brief = "PNG of an entity, prefab or template on its own in resources, written next frame",
        .params = {{ .name = "subject", .type = ecs_id(ecs_string_t) }},
        .callback = box_cmd_render,
        .ctx = state
    });
}
