/**
 * @file checkpoint/write.c
 * @brief Writes the entities a script owns back into a copy of its source.
 *
 * Every managed script is walked with an edit set: the current value of every
 * component of every entity the script owns is recorded and the edit set turns
 * them into new source, which leaves the text of unchanged statements alone.
 */

#include "checkpoint_internal.h"
#include <string.h>
#include <stdio.h>

/** Test whether an entity belongs to the flecs namespace.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is a child of flecs
 */
static bool box_checkpoint_builtin(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t parent = e;
    while (parent) {
        if (parent == EcsFlecs) {
            result = true;
            break;
        }

        parent = ecs_get_target(world, parent, EcsChildOf, 0);
    }

    return result;
}

bool box_checkpoint_writable(
    box_checkpoint_ctx_t *ctx,
    ecs_id_t id)
{
    bool result = false;
    ecs_world_t *world = ctx->world;
    ecs_map_val_t *cached = ecs_map_get(&ctx->ids, id);
    if (cached) {
        result = cached[0] != 0;
        goto done;
    }

    ecs_entity_t type = ecs_get_typeid(world, id);
    ecs_entity_t first = id & ECS_COMPONENT_MASK;
    if (ECS_IS_PAIR(id)) {
        first = ecs_pair_first(world, id);
    }

    result = type != 0 && ecs_has(world, type, EcsType) &&
        !box_checkpoint_builtin(world, first) &&
        !ecs_has_id(world, first, ecs_id(NoCheckpoint));
    ecs_map_insert(&ctx->ids, id, result);

done:
    return result;
}

/** Record the values of the components of an entity in an edit set.
 *
 * @param ctx The state of the write
 * @param script The parsed script that owns the entity
 * @param edits The edit set of the script
 * @param e The entity
 */
static void box_checkpoint_entity(
    box_checkpoint_ctx_t *ctx,
    const ecs_script_t *script,
    ecs_script_edits_t *edits,
    ecs_entity_t e)
{
    ecs_world_t *world = ctx->world;
    ecs_script_source_t source;
    if (!ecs_script_entity_source(script, e, &source)) {
        box_checkpoint_skip(ctx, e);
    } else if (source.template_) {
        ctx->templates ++;
    } else {
        const ecs_type_t *type = ecs_get_type(world, e);
        int32_t i, count = type ? type->count : 0;
        for (i = 0; i < count; i ++) {
            ecs_id_t id = type->array[i];
            const void *ptr = NULL;
            if (box_checkpoint_writable(ctx, id)) {
                ptr = ecs_get_id(world, e, id);
            }

            if (ptr && ecs_script_edits_set(edits, e, id, ptr)) {
                box_checkpoint_skip(ctx, e);
                break;
            }
        }
    }
}

/** Write the source of one script with the values of its entities to a file.
 *
 * @param ctx The state of the write
 * @param script The script entity
 * @return 0 when the file was written
 */
static int box_checkpoint_script(
    box_checkpoint_ctx_t *ctx,
    ecs_entity_t script)
{
    int result = -1;
    ecs_world_t *world = ctx->world;
    const EcsScript *code = ecs_get(world, script, EcsScript);
    ecs_script_edits_t *edits = ecs_script_edits_new(code->script);
    if (!edits) {
        ecs_warn("checkpoint: cannot edit script %s", ecs_get_name(world, script));
        goto done;
    }

    ecs_iter_t it = ecs_each_pair_t(world, EcsScript, script);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_checkpoint_entity(ctx, code->script, edits, it.entities[i]);
        }
    }

    char *text = ecs_script_edits_apply(edits);
    ecs_script_edits_free(edits);
    if (!text) {
        ecs_warn("checkpoint: failed to update script %s",
            ecs_get_name(world, script));
        goto done;
    }

    char *path = flecs_asprintf(
        "%s/%s", ctx->folder,
        box_checkpoint_name(&ctx->names, world, script));
    FILE *file = fopen(path, "w");
    if (file) {
        fputs(text, file);
        fclose(file);
        result = 0;
    } else {
        ecs_warn("checkpoint: failed to write %s", path);
    }

    ecs_os_free(path);
    ecs_os_free(text);

done:
    return result;
}

int32_t box_checkpoint_scripts(
    box_checkpoint_ctx_t *ctx)
{
    ecs_vec_t scripts;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    box_checkpoint_managed(ctx->world, &scripts);

    int32_t i, count = ecs_vec_count(&scripts), result = 0;
    ecs_entity_t *array = ecs_vec_first_t(&scripts, ecs_entity_t);
    for (i = 0; i < count; i ++) {
        if (!box_checkpoint_script(ctx, array[i])) {
            result ++;
        }
    }

    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);

    return result;
}

/** Test whether a table holds a component that a checkpoint writes.
 *
 * @param ctx The state of the write
 * @param table The table
 * @return true when one of the components of the table is written
 */
static bool box_checkpoint_table_writable(
    box_checkpoint_ctx_t *ctx,
    ecs_table_t *table)
{
    bool result = false;
    const ecs_type_t *type = ecs_table_get_type(table);
    int32_t i;
    for (i = 0; i < type->count; i ++) {
        if (box_checkpoint_writable(ctx, type->array[i])) {
            result = true;
            break;
        }
    }

    return result;
}

void box_checkpoint_unowned(
    box_checkpoint_ctx_t *ctx)
{
    ecs_world_t *world = ctx->world;
    ecs_query_t *query = ecs_query(world, {
        .terms = {
            { .id = ecs_pair(EcsChildOf, EcsFlecs),
              .oper = EcsNot, .src.id = EcsSelf|EcsUp },
            { .id = EcsModule, .oper = EcsNot, .src.id = EcsSelf|EcsUp },
            { .id = ecs_pair_t(EcsScript, EcsWildcard), .oper = EcsNot }
        },
        .flags = EcsQueryMatchDisabled|EcsQueryMatchPrefab
    });

    ecs_assert(query != NULL, ECS_INVALID_OPERATION, "failed to create query");

    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
        if (!box_checkpoint_table_writable(ctx, it.table)) {
            continue;
        }

        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_checkpoint_skip(ctx, it.entities[i]);
        }
    }

    ecs_query_fini(query);
}
