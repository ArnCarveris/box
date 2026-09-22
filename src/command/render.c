/**
 * @file command/render.c
 * @brief The render command: renders an entity, prefab or template on its own.
 *
 * The command builds a temporary view with its own camera, light, layer and
 * render target, frames the camera on the bounds of the subject and captures
 * the target to a PNG in the session resources folder. The temporary scene is
 * deleted again when the image is on disk.
 */

#include "command_internal.h"
#include "../render/render_internal.h"
#include <math.h>
#include <string.h>

/** Degrees in one radian, the unit the camera field of view is given in */
#define BOX_RENDER_CMD_RADIANS (3.14159265f / 180.0f)

/** Vertical field of view in degrees of the camera that frames the subject */
#define BOX_RENDER_CMD_FOV (45.0f)

/** Width and height in pixels of the image a render writes */
#define BOX_RENDER_CMD_SIZE (512)

/** Smallest bounding radius a subject is framed with, in world units */
#define BOX_RENDER_CMD_RADIUS (0.5f)

/** Radiance scale of the light that shines on the subject */
#define BOX_RENDER_CMD_LIGHT (3.0f)

/** Order of the temporary view, which renders before the views of the scene */
#define BOX_RENDER_CMD_PRIORITY (-1000)

/** X of the direction from the subject to the camera, 45 degrees to the right */
#define BOX_RENDER_CMD_EYE_X (0.6124f)

/** Y of the direction from the subject to the camera, 30 degrees above it */
#define BOX_RENDER_CMD_EYE_Y (0.5f)

/** Z of the direction from the subject to the camera, in front of it */
#define BOX_RENDER_CMD_EYE_Z (-0.6124f)

/** X of the direction from the subject to the light, to the left */
#define BOX_RENDER_CMD_SUN_X (-0.5f)

/** Y of the direction from the subject to the light, above it */
#define BOX_RENDER_CMD_SUN_Y (1.0f)

/** Z of the direction from the subject to the light, in front of it */
#define BOX_RENDER_CMD_SUN_Z (-0.6f)

/** Height above the scene at which a prefab or template instance is rendered */
#define BOX_RENDER_CMD_SCRATCH (10000.0f)

/** Frames a render waits for the bounds of its subject before it frames anyway */
#define BOX_RENDER_CMD_WAIT (4)

/** Frames a render waits for its image before it cleans up without one */
#define BOX_RENDER_CMD_TIMEOUT (600)

/** Test whether an entity is a script template.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is instantiated by adding it as a component
 */
static bool box_render_cmd_is_template(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsScript *script = ecs_get(world, e, EcsScript);

    return script != NULL && script->template_ != NULL;
}

/** Instantiate a prefab or template under the scratch scope of a session.
 *
 * The scope sits far above the scene, so a subject that is not part of the
 * scene is rendered without the scene around it.
 *
 * @param world The world
 * @param session The session entity
 * @param subject The prefab or template
 * @return The instance, 0 when the subject is a live entity
 */
static ecs_entity_t box_render_cmd_instance(
    ecs_world_t *world,
    ecs_entity_t session,
    ecs_entity_t subject)
{
    ecs_entity_t result = 0;
    bool prefab = ecs_has_id(world, subject, EcsPrefab);
    if (!prefab && !box_render_cmd_is_template(world, subject)) {
        goto done;
    }

    ecs_entity_t scope = ecs_entity(world, {
        .name = "scratch", .parent = session
    });

    ecs_set(world, scope, Position, {0, BOX_RENDER_CMD_SCRATCH, 0});
    result = ecs_new_w_pair(world, EcsChildOf, scope);
    if (prefab) {
        ecs_add_pair(world, result, EcsIsA, subject);
    } else {
        ecs_add_id(world, result, subject);
    }

    if (!ecs_has(world, result, Position)) {
        ecs_set(world, result, Position, {0, 0, 0});
    }

done:
    return result;
}

/** Collect an entity and its descendants, a parent before its children.
 *
 * @param world The world
 * @param e The entity
 * @param out Vector receiving the entities
 */
static void box_render_cmd_collect(
    ecs_world_t *world,
    ecs_entity_t e,
    void *out)
{
    ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = e;

    box_hierarchy_children(world, e, box_render_cmd_collect, out);
}

/** Put an entity and its descendants in the layer of a render.
 *
 * @param world The world
 * @param entities The entities
 * @param layer The layer entity
 */
static void box_render_cmd_join(
    ecs_world_t *world,
    const ecs_vec_t *entities,
    ecs_entity_t layer)
{
    int32_t i, count = ecs_vec_count(entities);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(entities, ecs_entity_t, i)[0];
        if (!ecs_has_pair(world, e, Layer, layer)) {
            ecs_add_pair(world, e, Layer, layer);
        }
    }
}

/** Union the world bounds of the entities that hold geometry.
 *
 * An entity without geometry carries an empty AABB at the origin, which the
 * union skips so it does not pull the bounds of the subject to the origin.
 *
 * @param world The world
 * @param entities The entities
 * @param min Receives the minimum corner
 * @param max Receives the maximum corner
 * @return true when at least one entity has bounds
 */
static bool box_render_cmd_bounds(
    ecs_world_t *world,
    const ecs_vec_t *entities,
    vec3 *min,
    vec3 *max)
{
    bool result = false;
    int32_t i, count = ecs_vec_count(entities);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(entities, ecs_entity_t, i)[0];
        const AABB *aabb = ecs_get(world, e, AABB);
        bool empty = !aabb || (aabb->min.x == aabb->max.x &&
            aabb->min.y == aabb->max.y && aabb->min.z == aabb->max.z);
        if (empty) {
            continue;
        }

        min[0] = result ? vec3_min(min[0], aabb->min) : aabb->min;
        max[0] = result ? vec3_max(max[0], aabb->max) : aabb->max;
        result = true;
    }

    return result;
}

/** Get the point a subject without bounds is framed on.
 *
 * @param world The world
 * @param subject The subject entity
 * @return The world position of the subject, the origin when it has none
 */
static vec3 box_render_cmd_origin(
    ecs_world_t *world,
    ecs_entity_t subject)
{
    vec3 result = vec3_new(0, 0, 0);
    const WorldTransform *transform = ecs_get(world, subject, WorldTransform);
    if (transform) {
        result = mat4_get_translation(&transform->m);
    }

    return result;
}

/** Point the camera and the light of a render at the bounds of its subject.
 *
 * The camera sits on a three quarter view above and in front of the subject,
 * far enough that a sphere around the bounds fills the field of view.
 *
 * @param world The world
 * @param job The render
 * @param min Minimum corner of the subject
 * @param max Maximum corner of the subject
 */
static void box_render_cmd_frame(
    ecs_world_t *world,
    const box_render_job_t *job,
    vec3 min,
    vec3 max)
{
    vec3 center = vec3_scale(vec3_add(min, max), 0.5f);
    float radius = vec3_length(vec3_sub(max, min)) * 0.5f;
    if (radius < BOX_RENDER_CMD_RADIUS) {
        radius = BOX_RENDER_CMD_RADIUS;
    }

    float distance = radius / sinf(BOX_RENDER_CMD_FOV * 0.5f * BOX_RENDER_CMD_RADIANS);
    vec3 eye = vec3_add(center, vec3_scale(vec3_new(BOX_RENDER_CMD_EYE_X,
        BOX_RENDER_CMD_EYE_Y, BOX_RENDER_CMD_EYE_Z), distance));
    vec3 sun = vec3_add(center, vec3_scale(vec3_new(BOX_RENDER_CMD_SUN_X,
        BOX_RENDER_CMD_SUN_Y, BOX_RENDER_CMD_SUN_Z), distance));

    ecs_set(world, job->camera, Position, {eye.x, eye.y, eye.z});
    ecs_set(world, job->camera, LookAt, {center.x, center.y, center.z});
    ecs_set(world, job->light, Position, {sun.x, sun.y, sun.z});
    ecs_set(world, job->light, LookAt, {center.x, center.y, center.z});
}

/** Create the view that renders the layer of a render into its target.
 *
 * The view is strict, so it draws the members of the layer of the render only
 * and a live subject renders without the scene around it.
 *
 * @param world The world
 * @param job The render
 * @return The view entity
 */
static ecs_entity_t box_render_cmd_view(
    ecs_world_t *world,
    const box_render_job_t *job)
{
    ecs_entity_t result = ecs_new(world);
    View *view = ecs_ensure(world, result, View);
    view->camera = job->camera;
    view->target = job->target;
    view->layers[0] = job->layer;
    view->shadow_layers[0] = job->layer;
    view->strict_layers = true;
    view->priority = BOX_RENDER_CMD_PRIORITY;
    ecs_modified(world, result, View);

    ecs_set(world, result, EnvironmentLight, {
        .sky_color = { BOX_ENV_SKY, BOX_ENV_SKY, BOX_ENV_SKY },
        .ground_color = { BOX_ENV_GROUND, BOX_ENV_GROUND, BOX_ENV_GROUND },
        .intensity = BOX_ENV_INTENSITY
    });

    return result;
}

/** Delete the temporary entities of a render and release its path.
 *
 * @param world The world
 * @param job The render
 */
static void box_render_cmd_cleanup(
    ecs_world_t *world,
    box_render_job_t *job)
{
    ecs_entity_t entities[] = {
        job->scratch, job->view, job->camera, job->light, job->target, job->layer
    };

    int32_t i;
    for (i = 0; i < 6; i ++) {
        if (entities[i] && ecs_is_alive(world, entities[i])) {
            ecs_delete(world, entities[i]);
        }
    }

    ecs_os_free(job->path);
    job->path = NULL;
}

/** Test whether the renderer wrote the image of a render.
 *
 * @param path The file
 * @return true when the file exists
 */
static bool box_render_cmd_written(
    const char *path)
{
    bool result = false;
    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
        result = true;
    }

    return result;
}

/** Frame a render and ask its camera for the image once the bounds are known.
 *
 * The bounds of a subject that was just instantiated are written by the first
 * extract that sees it, so a render waits a few frames for them.
 *
 * @param world The world
 * @param job The render
 * @return true when the capture was requested
 */
static bool box_render_cmd_request(
    ecs_world_t *world,
    box_render_job_t *job)
{
    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    box_render_cmd_collect(world, job->subject, &entities);
    box_render_cmd_join(world, &entities, job->layer);

    vec3 min = vec3_new(0, 0, 0), max = vec3_new(0, 0, 0);
    bool bounds = box_render_cmd_bounds(world, &entities, &min, &max);
    bool result = bounds || job->frames >= BOX_RENDER_CMD_WAIT;
    if (result) {
        if (!bounds) {
            min = max = box_render_cmd_origin(world, job->subject);
        }

        box_render_cmd_frame(world, job, min, max);
        box_capture_request(world, job->camera, ecs_os_strdup(job->path));
        job->requested = true;
    }

    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);

    return result;
}

/** Advance the pending renders of a session and clean up the finished ones.
 *
 * @param world The world
 * @param s The session state
 */
static void box_render_cmd_tick(
    ecs_world_t *world,
    TerminalSession *s)
{
    int32_t i = 0;
    while (i < ecs_vec_count(&s->renders)) {
        box_render_job_t *job = ecs_vec_get_t(&s->renders, box_render_job_t, i);
        bool done = job->frames >= BOX_RENDER_CMD_TIMEOUT;
        if (!done && !job->requested) {
            box_render_cmd_request(world, job);
        } else if (!done) {
            done = box_render_cmd_written(job->path);
        }

        if (done && !job->requested) {
            ecs_warn("render of %s timed out without an image",
                ecs_get_name(world, job->subject));
        }

        job->frames ++;
        if (!done) {
            i ++;
            continue;
        }

        box_render_cmd_cleanup(world, job);
        ecs_vec_remove_t(&s->renders, box_render_job_t, i);
    }

}

/** Advance the renders of every session once per frame.
 *
 * @param it Iterator over the sessions
 */
static void BoxRenderCommandPending(
    ecs_iter_t *it)
{
    TerminalSession *s = ecs_field(it, TerminalSession, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (ecs_vec_count(&s[i].renders)) {
            box_render_cmd_tick(it->world, &s[i]);
        }
    }
}

/** Build the temporary scene of a render and queue it on its session.
 *
 * @param world The world
 * @param session The session entity
 * @param subject The entity, prefab or template to render
 * @return The name of the image in the resources folder, the caller frees it
 */
static char* box_render_cmd_start(
    ecs_world_t *world,
    ecs_entity_t session,
    ecs_entity_t subject)
{
    TerminalSession *s = ecs_get_mut(world, session, TerminalSession);
    ecs_assert(s != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_OPERATION, "import box.core first");

    box_render_job_t job = {0};
    job.scratch = box_render_cmd_instance(world, session, subject);
    job.subject = job.scratch ? job.scratch : subject;
    job.layer = ecs_new(world);
    job.target = ecs_new(world);
    ecs_set(world, job.target, RenderTarget, {
        BOX_RENDER_CMD_SIZE, BOX_RENDER_CMD_SIZE
    });

    job.camera = ecs_new(world);
    ecs_set(world, job.camera, Camera, { .fov = BOX_RENDER_CMD_FOV });
    job.light = ecs_new(world);
    ecs_set(world, job.light, DirectionalLight, { .intensity = BOX_RENDER_CMD_LIGHT });
    ecs_add_pair(world, job.light, Layer, job.layer);
    job.view = box_render_cmd_view(world, &job);

    char *result = flecs_asprintf("render_%d_%d.png", app->frame, s->render_count ++);
    job.path = box_capture_path(world, session, result);
    ecs_vec_append_t(NULL, &s->renders, box_render_job_t)[0] = job;

    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    box_render_cmd_collect(world, job.subject, &entities);
    box_render_cmd_join(world, &entities, job.layer);
    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);

    return result;
}

/** The render command: render one entity, prefab or template on its own.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_render_cmd_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *arg = *(char**)argv[1].ptr;
    ecs_entity_t cwd = box_path_cwd(world, session);
    ecs_entity_t subject = 0;
    char *out = NULL;
    if (box_path_lookup(world, cwd, arg, &subject) || !subject) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "unknown entity '%s'", arg);
        goto done;
    }

    char *file = box_render_cmd_start(world, session, subject);
    out = flecs_asprintf("%s\n", file);
    ecs_os_free(file);
done:
    *(char**)result->ptr = out;
}

void box_render_cmd_fini(
    TerminalSession *s)
{
    int32_t i, count = ecs_vec_count(&s->renders);
    for (i = 0; i < count; i ++) {
        ecs_os_free(ecs_vec_get_t(&s->renders, box_render_job_t, i)->path);
    }

    ecs_vec_fini_t(NULL, &s->renders, box_render_job_t);
}

void box_render_cmd_init(
    ecs_world_t *world)
{
    box_command_param_t params[] = {{
        .name = "subject", .type = ecs_id(ecs_string_t)
    }};

    box_command_register(world, "render",
        "Render an entity, prefab or template on its own to the session resources",
        params, 1, box_render_cmd_command);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxRenderCommandPending" }),
        .phase = EcsOnLoad,
        .query.terms = {{ .id = ecs_id(TerminalSession) }},
        .callback = BoxRenderCommandPending,
        .immediate = true
    });
}
