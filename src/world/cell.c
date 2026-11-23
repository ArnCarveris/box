/**
 * @file world/cell.c
 * @brief WorldCell roots: anchored at a whole cell of the absolute world, placed in the frame
 * of the local origin in f64, with their descendants moving along on the GPU.
 */

#include "world.h"

void box_world_cell_position(
    const WorldCell *cell,
    const box_world_frame_t *frame,
    Position *out)
{
    double corner[3] = {
        (double)cell->x * BOX_WORLD_CELL_SIZE,
        (double)cell->y * BOX_WORLD_CELL_SIZE,
        (double)cell->z * BOX_WORLD_CELL_SIZE
    };
    double local[3];

    box_world_frame_to_local(frame, corner, local);
    out->x = (float)local[0];
    out->y = (float)local[1];
    out->z = (float)local[2];
}

/** Place the roots whose WorldCell was set at their cell in the frame of the local origin,
 * turned and scaled into it when the frame turns or scales.
 *
 * @param it The iterator of the observer
 */
static void box_world_cell_on_set(
    ecs_iter_t *it)
{
    const WorldCell *cells = ecs_field(it, WorldCell, 0);
    box_world_frame_t frame;
    int32_t i;

    box_world_frame_get(it->world, &frame);
    bool framed = !box_world_frame_is_translation(&frame);
    Rotation turned = {0};
    float s = (float)(1.0 / frame.s);

    if (framed) {
        box_world_rotation_of_matrix(frame.r, &turned);
    }

    for (i = 0; i < it->count; i ++) {
        Position p;
        box_world_cell_position(&cells[i], &frame, &p);
        ecs_set_ptr(it->world, it->entities[i], Position, &p);
        if (framed) {
            ecs_set_ptr(it->world, it->entities[i], Rotation, &turned);
            ecs_set(it->world, it->entities[i], Scale, {s, s, s});
        }
    }
}

void box_world_cell_import(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(WorldCell), EcsWith, ecs_id(Scale));

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldCell) }},
        .events = { EcsOnSet },
        .callback = box_world_cell_on_set
    });
}
