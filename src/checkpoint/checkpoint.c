/**
 * @file checkpoint/checkpoint.c
 * @brief Registers the box.checkpoint module and writes checkpoint folders.
 */

#define BOX_CHECKPOINT_IMPL
#include "checkpoint_internal.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

ECS_COMPONENT_DECLARE(Checkpoint);
ECS_TAG_DECLARE(NoCheckpoint);

/** Components of the box modules that hold state the engine derives */
static const char *box_checkpoint_derived[] = {
    "box.transform.WorldTransform",
    "box.transform.AABB",
    "box.render.RenderMesh",
    "box.render.RenderTexture",
    "box.collision.Collider",
    "box.collision.RootBounds",
    "box.material.ActualTint",
    "box.picking.PickHit",
    "box.camera.Capture",
    "box.input.Input",
    "box.input.InputEvent",
    "box.input.InputAction",
    "box.input.ActionEvent",
    "box.input.KeyState",
    "box.input.Mouse",
    "box.command.Terminal",
    "box.command.TerminalInput",
    "box.command.TerminalOutput",
    "box.command.HttpServer",
    "box.core.App",
    "box.surface.Surface",
    "box.job.JobStats",
    "box.script.ScriptStats",
    "box.shader.ShaderStatus",
    "box.spline.SplineInfo",
    "box.monitor.CpuMonitor",
    "box.monitor.SystemTime",
    "box.monitor.PhaseTime",
    "box.monitor.EcsStats",
    "box.monitor.SystemMonitor",
    "box.monitor.Fps",
    "box.ui.Bounds",
    NULL
};

/** Mark the derived components of the modules the world has imported.
 *
 * The modules are imported in any order, so the components are looked up when
 * a checkpoint is written instead of when the module is registered.
 *
 * @param world The world
 */
static void box_checkpoint_mark_derived(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; box_checkpoint_derived[i]; i ++) {
        ecs_entity_t e = ecs_lookup(world, box_checkpoint_derived[i]);
        if (e && !ecs_has_id(world, e, ecs_id(NoCheckpoint))) {
            ecs_add_id(world, e, ecs_id(NoCheckpoint));
        }
    }
}

/** Compare two entity paths of a report.
 *
 * @param a The first path
 * @param b The second path
 * @return Negative when a sorts before b, positive when it sorts after
 */
static int box_checkpoint_cmp(
    const void *a,
    const void *b)
{
    return ecs_os_strcmp(*(char* const*)a, *(char* const*)b);
}

char* box_checkpoint_report(
    box_checkpoint_ctx_t *ctx)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(&ctx->skipped);
    char **paths = ecs_vec_first_t(&ctx->skipped, char*);
    qsort(paths, (size_t)count, sizeof(char*), box_checkpoint_cmp);

    for (i = 0; i < count && i < BOX_CHECKPOINT_REPORT_MAX; i ++) {
        ecs_strbuf_append(&buf, "%s%s\n", ctx->prefix, paths[i]);
    }

    if (count > BOX_CHECKPOINT_REPORT_MAX) {
        ecs_strbuf_append(&buf, "%s%d more entities\n", ctx->prefix,
            count - BOX_CHECKPOINT_REPORT_MAX);
    }

    if (ctx->templates) {
        ecs_strbuf_append(&buf,
            BOX_CHECKPOINT_SKIPPED "%d entities of a template body\n",
            ctx->templates);
    }

    char *result = ecs_strbuf_get(&buf);

    return result ? result : ecs_os_strdup("");
}

void box_checkpoint_ctx_init(
    box_checkpoint_ctx_t *ctx,
    ecs_world_t *world,
    const char *folder,
    const char *prefix)
{
    ctx->world = world;
    ctx->folder = folder;
    ctx->prefix = prefix;
    ctx->templates = 0;
    ecs_map_init(&ctx->ids, NULL);
    ecs_vec_init_t(NULL, &ctx->skipped, char*, 0);
    ecs_vec_init_t(NULL, &ctx->names, char*, 0);
}

void box_checkpoint_ctx_fini(
    box_checkpoint_ctx_t *ctx)
{
    int32_t i, count = ecs_vec_count(&ctx->skipped);
    char **paths = ecs_vec_first_t(&ctx->skipped, char*);
    for (i = 0; i < count; i ++) {
        ecs_os_free(paths[i]);
    }

    count = ecs_vec_count(&ctx->names);
    char **names = ecs_vec_first_t(&ctx->names, char*);
    for (i = 0; i < count; i ++) {
        ecs_os_free(names[i]);
    }

    ecs_vec_fini_t(NULL, &ctx->skipped, char*);
    ecs_vec_fini_t(NULL, &ctx->names, char*);
    ecs_map_fini(&ctx->ids);
}

void box_checkpoint_skip(
    box_checkpoint_ctx_t *ctx,
    ecs_entity_t e)
{
    char **elem = ecs_vec_append_t(NULL, &ctx->skipped, char*);
    elem[0] = ecs_get_path(ctx->world, e);
}

int box_checkpoint_mkdir(
    const char *path)
{
    int result = 0;
    char *copy = ecs_os_strdup(path);
    char *p;
    for (p = copy + 1; p[0]; p ++) {
        if (p[0] == '/') {
            p[0] = '\0';
            mkdir(copy, 0777);
            p[0] = '/';
        }
    }

    if (mkdir(copy, 0777) && errno != EEXIST) {
        result = -1;
    }

    ecs_os_free(copy);

    return result;
}

bool box_checkpoint_in_module(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t parent = ecs_get_target(world, e, EcsChildOf, 0);
    while (parent && !result) {
        result = ecs_has_id(world, parent, EcsModule);
        parent = ecs_get_target(world, parent, EcsChildOf, 0);
    }

    return result;
}

/** Test whether a file lies in the folder of the file of a script under a module.
 *
 * @param world The world
 * @param file The file
 * @return true when a script under a module was loaded from the folder or above it
 */
static bool box_checkpoint_framework_file(
    const ecs_world_t *world,
    const char *file)
{
    bool result = false;
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (!result && ecs_each_next(&it)) {
        const EcsScript *code = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; !result && i < it.count; i ++) {
            const char *owner = code[i].filename;
            const char *slash = owner ? strrchr(owner, '/') : NULL;
            result = slash && !strncmp(owner, file, (size_t)(slash - owner + 1)) &&
                box_checkpoint_in_module(world, it.entities[i]);
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

bool box_checkpoint_framework(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = box_checkpoint_in_module(world, e);
    const EcsScript *code = ecs_get(world, e, EcsScript);
    if (!result && code && code->filename) {
        result = box_checkpoint_framework_file(world, code->filename);
    }

    return result;
}

void box_checkpoint_managed(
    ecs_world_t *world,
    ecs_vec_t *scripts)
{
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *code = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (code[i].script && !code[i].template_ &&
                !box_checkpoint_framework(world, it.entities[i]))
            {
                ecs_vec_append_t(NULL, scripts, ecs_entity_t)[0] =
                    it.entities[i];
            }
        }
    }
}

const char* box_checkpoint_name(
    ecs_vec_t *names,
    ecs_world_t *world,
    ecs_entity_t script)
{
    const EcsScript *code = ecs_get(world, script, EcsScript);
    const char *file = code->filename;
    char *name = NULL;
    if (file) {
        const char *slash = strrchr(file, '/');
        name = ecs_os_strdup(slash ? slash + 1 : file);
    } else {
        char *path = ecs_get_path(world, script);
        name = flecs_asprintf("%s.flecs", path);
        ecs_os_free(path);
    }

    int32_t i, count = ecs_vec_count(names);
    char **array = ecs_vec_first_t(names, char*);
    for (i = 0; i < count; i ++) {
        if (!ecs_os_strcmp(array[i], name)) {
            char *unique = flecs_asprintf("%d_%s", count, name);
            ecs_os_free(name);
            name = unique;
            break;
        }
    }

    char **elem = ecs_vec_append_t(NULL, names, char*);
    elem[0] = name;

    return name;
}

char* box_checkpoint_dir(
    ecs_world_t *world)
{
    char *result = NULL;
    const Checkpoint *cp = ecs_singleton_get(world, Checkpoint);
    if (cp && cp->dir && cp->dir[0]) {
        result = ecs_os_strdup(cp->dir);
    } else {
        const App *app = ecs_singleton_get(world, App);
        ecs_assert(app != NULL, ECS_INVALID_OPERATION,
            "import box.core before box.checkpoint");
        ecs_assert(app->process_dir != NULL, ECS_INVALID_OPERATION,
            "no process folder");
        result = flecs_asprintf("%s/checkpoints", app->process_dir);
    }

    return result;
}

char* box_checkpoint_write(
    ecs_world_t *world,
    const char *name,
    char **report)
{
    char *result = NULL;
    box_checkpoint_mark_derived(world);
    char *root = box_checkpoint_dir(world);
    int32_t index = ecs_singleton_ensure(world, Checkpoint)->count;
    if (name && name[0]) {
        result = flecs_asprintf("%s/%s", root, name);
    } else {
        result = flecs_asprintf("%s/%d", root, index);
    }

    ecs_os_free(root);

    if (box_checkpoint_mkdir(result)) {
        ecs_err("failed to create checkpoint folder %s", result);
        ecs_os_free(result);
        result = NULL;
        goto done;
    }

    box_checkpoint_ctx_t ctx;
    box_checkpoint_ctx_init(&ctx, world, result, BOX_CHECKPOINT_SKIPPED);

    box_checkpoint_scripts(&ctx);
    box_checkpoint_unowned(&ctx);

    char *text = box_checkpoint_report(&ctx);
    if (report) {
        report[0] = text;
    } else {
        ecs_os_free(text);
    }

    box_checkpoint_ctx_fini(&ctx);

    Checkpoint *cp = ecs_singleton_ensure(world, Checkpoint);
    ecs_os_strset(&cp->last, result);
    cp->count = index + 1;
    ecs_singleton_modified(world, Checkpoint);

done:
    return result;
}

void BoxCheckpointImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxCore);

    ECS_META_COMPONENT(world, Checkpoint);
    ECS_TAG_DEFINE(world, NoCheckpoint);

    ecs_singleton_set(world, Checkpoint, { 0 });
}
