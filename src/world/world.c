/**
 * @file world/world.c
 * @brief Registers the box.world module and finds the focus of the streaming systems.
 */

#define BOX_WORLD_IMPL
#include "world.h"

ECS_TAG_DECLARE(WorldFocus);
ECS_COMPONENT_DECLARE(ChunkGridGate);

ecs_entity_t box_world_scene_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    while (e && !result) {
        if (ecs_has(world, e, Scene)) {
            result = e;
        }

        e = ecs_get_parent(world, e);
    }

    return result;
}

bool box_world_is_root(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = true;
    ecs_entity_t parent = ecs_get_parent(world, e);
    while (parent && result) {
        result = !ecs_has(world, parent, WorldTransform);
        parent = ecs_get_parent(world, parent);
    }

    return result;
}

bool box_world_position(
    const ecs_world_t *world,
    ecs_entity_t e,
    dvec3 out)
{
    const Position *p = ecs_get(world, e, Position);
    WorldTransform buf;
    const WorldTransform *t = NULL;
    bool result = false;
    if (p && !ecs_has_id(world, e, ManualTransform) && box_world_is_root(world, e)) {
        out[0] = p->x;
        out[1] = p->y;
        out[2] = p->z;
        result = true;
    } else if ((t = box_transform_world(world, e, &buf))) {
        box_dvec3_copy(t->position, out);
        result = true;
    }

    return result;
}

/** Find the camera of the first View by order that has one.
 *
 * @param world The world
 * @return The camera, 0 when no view has one
 */
static ecs_entity_t box_world_view_camera(
    const ecs_world_t *world)
{
    ecs_entity_t result = 0, first = 0;
    int32_t order = 0, i;
    ecs_iter_t it = ecs_each(ECS_CONST_CAST(ecs_world_t*, world), View);
    while (ecs_each_next(&it)) {
        const View *views = ecs_field(&it, View, 0);
        for (i = 0; i < it.count; i ++) {
            if (!views[i].camera || !ecs_is_alive(world, views[i].camera)) {
                continue;
            }

            if (!first || box_view_compare(views[i].order, it.entities[i], order, first) < 0) {
                first = it.entities[i];
                order = views[i].order;
                result = views[i].camera;
            }
        }
    }

    return result;
}

/** Add a focus to the output when it has a place in the world.
 *
 * @param world The world
 * @param e The entity
 * @param out The foci, NULL to count them
 * @param max Foci out holds
 * @param count Foci so far, incremented for the entity
 */
static void box_world_focus_add(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_world_focus_t *out,
    int32_t max,
    int32_t *count)
{
    dvec3 position;
    if (!box_world_position(world, e, position)) {
        goto done;
    }

    if (out && *count < max) {
        out[*count].entity = e;
        out[*count].scene = box_world_scene_of(world, e);
        box_dvec3_copy(position, out[*count].position);
    }

    (*count) ++;
done:
    return;
}

int32_t box_world_focus(
    const ecs_world_t *world,
    box_world_focus_t *out,
    int32_t max)
{
    int32_t result = 0, i;
    ecs_iter_t it = WorldFocus
        ? ecs_each_id(ECS_CONST_CAST(ecs_world_t*, world), WorldFocus)
        : (ecs_iter_t){0};
    while (WorldFocus && ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_world_focus_add(world, it.entities[i], out, max, &result);
        }
    }

    if (!result) {
        ecs_entity_t camera = box_world_view_camera(world);
        if (camera) {
            box_world_focus_add(world, camera, out, max, &result);
        }
    }

    return result;
}

void BoxWorldImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxWorld);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);

    ECS_TAG_DEFINE(world, WorldFocus);
    ECS_META_COMPONENT(world, ChunkGrid);
    ECS_META_COMPONENT(world, ChunkGridStatus);
    ECS_META_COMPONENT(world, ChunkGridCell);
    ECS_META_COMPONENT(world, ScatterMode);
    ECS_META_COMPONENT(world, ScatterItem);
    ECS_META_COMPONENT(world, Scatter);
    ECS_META_COMPONENT(world, ScatterStatus);

    box_world_chunk_import(world);
    box_world_scatter_import(world);
}
