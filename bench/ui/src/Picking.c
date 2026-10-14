/**
 * @file Picking.c
 * @brief Benchmarks for picking a HUD of 10k UI elements over a 3D scene of 100k shapes.
 *
 * The HUD covers part of the window, so a pick tests the UI and, where no element handles the
 * event, the shapes behind it. Cases move the cursor, keep it still and leave it unknown.
 */

#include <box.h>
#include <bake_bench.h>

/** Rows and columns of the UI grid */
#define BENCH_UI_SIDE (100)

/** Rows and columns of the shape grid */
#define BENCH_SHAPE_SIDE (317)

/** Create the scene, the HUD and their views.
 *
 * @return The world
 */
static ecs_world_t* bench_picking_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 300});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t x, y;
    for (y = 0; y < BENCH_SHAPE_SIDE; y ++) {
        for (x = 0; x < BENCH_SHAPE_SIDE; x ++) {
            ecs_entity_t e = ecs_new(world);
            ecs_set(world, e, Position, {(float)x - BENCH_SHAPE_SIDE / 2,
                (float)y - BENCH_SHAPE_SIDE / 2, 0});
            ecs_set(world, e, Box, {0.8f, 0.8f, 0.8f});
            ecs_add(world, e, Interactive);
        }
    }

    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t hud_camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_camera, Position, {0, 0, 100});
    ecs_set(world, hud_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});
    ecs_entity_t panel = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, panel, Position, {0, 0, 0});
    ecs_set(world, panel, Rect, {400, 300});
    for (y = 0; y < BENCH_UI_SIDE; y ++) {
        for (x = 0; x < BENCH_UI_SIDE; x ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, panel);
            ecs_set(world, e, Position, {(float)x * 4 - 198, (float)y * 3 - 148, 1});
            ecs_set(world, e, Rect, {3, 2});
        }
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

void Picking_move_10k_ui_100k_shapes(
    bench_t *b)
{
    // ui/picking
    ecs_world_t *world = bench_picking_world();
    int32_t i = 0;
    while (bench_iter(b)) {
        float x = 340.0f + (float)((i * 37) % 600);
        float y = 160.0f + (float)((i * 23) % 400);
        box_input_mouse_move(world, x, y, false);
        ecs_progress(world, 0);
        i ++;
    }

    ecs_fini(world);
}

void Picking_still_cursor_10k_ui_100k_shapes(
    bench_t *b)
{
    // ui/picking
    ecs_world_t *world = bench_picking_world();
    box_input_mouse_move(world, 700, 300, false);
    ecs_progress(world, 0);
    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}

void Picking_no_cursor_10k_ui_100k_shapes(
    bench_t *b)
{
    // ui/picking
    ecs_world_t *world = bench_picking_world();
    while (bench_iter(b)) {
        ecs_progress(world, 0);
    }

    ecs_fini(world);
}
