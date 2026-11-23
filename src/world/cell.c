/**
 * @file world/cell.c
 * @brief WorldCell roots: anchored at a whole cell of the absolute world, placed relative to
 * the local origin in f64, with their descendants moving along on the GPU.
 */

#include "world.h"

void box_world_cell_position(
    const WorldCell *cell,
    const double origin[3],
    Position *out)
{
    out->x = (float)((double)cell->x * BOX_WORLD_CELL_SIZE - origin[0]);
    out->y = (float)((double)cell->y * BOX_WORLD_CELL_SIZE - origin[1]);
    out->z = (float)((double)cell->z * BOX_WORLD_CELL_SIZE - origin[2]);
}

/** Place the roots whose WorldCell was set at their cell relative to the local origin.
 *
 * @param it The iterator of the observer
 */
static void box_world_cell_on_set(
    ecs_iter_t *it)
{
    const WorldCell *cells = ecs_field(it, WorldCell, 0);
    const WorldOrigin *o = ecs_singleton_get(it->world, WorldOrigin);
    double origin[3] = {0, 0, 0};
    int32_t i;

    if (o) {
        origin[0] = o->x;
        origin[1] = o->y;
        origin[2] = o->z;
    }

    for (i = 0; i < it->count; i ++) {
        Position p;
        box_world_cell_position(&cells[i], origin, &p);
        ecs_set_ptr(it->world, it->entities[i], Position, &p);
    }
}

void box_world_cell_import(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, ecs_id(Position));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldCell) }},
        .events = { EcsOnSet },
        .callback = box_world_cell_on_set
    });
}
