/**
 * @file render/scene.c
 * @brief Scene membership of entities through a cached query with up(Scene).
 *
 * An entity belongs to itself when it has Scene, else to its nearest ancestor with Scene, else
 * to the default scene. The query caches the source of the Scene term per table, so finding
 * the scene of an entity is a table lookup, never a walk up the hierarchy.
 */

#include "render.h"

/** Singleton that holds the scene membership query of a world */
typedef struct RenderScenes {
    ecs_query_t *query; /**< Entities with Scene on themselves or an ancestor */
} RenderScenes;

/** Component id of RenderScenes */
static ECS_COMPONENT_DECLARE(RenderScenes);

ecs_entity_t box_render_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_iter_t it;
    const RenderScenes *scenes = ecs_singleton_get(world, RenderScenes);
    if (!scenes || !scenes->query || !e) {
        goto done;
    }

    if (ecs_query_has(scenes->query, e, &it)) {
        result = ecs_field_is_self(&it, 0) ? e : ecs_field_src(&it, 0);
        ecs_iter_fini(&it);
    }

done:
    return result;
}

ecs_entity_t box_render_scene_field(
    const ecs_iter_t *it,
    int8_t field,
    int32_t row)
{
    ecs_entity_t result = 0;
    if (!ecs_field_is_set(it, field)) {
        goto done;
    }

    result = ecs_field_is_self(it, field) ? it->entities[row] : ecs_field_src(it, field);
done:
    return result;
}

void box_render_scenes_init(
    ecs_world_t *world)
{
    ecs_set_name_prefix(world, "Render");
    ECS_COMPONENT_DEFINE(world, RenderScenes);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, RenderScenes, { .ctor = flecs_default_ctor });
    ecs_query_t *q = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "SceneMembers" }),
        .terms = {{
            .id = ecs_id(Scene),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsChildOf,
            .inout = EcsInOutNone
        }},
        .cache_kind = EcsQueryCacheAuto,
        .flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled
    });
    ecs_singleton_set(world, RenderScenes, { .query = q });
}
