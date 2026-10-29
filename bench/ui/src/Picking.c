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

/** Create a 3D scene of 100k shapes behind a HUD whose 10k elements have LayoutBounds and no
 * shape, laid out in 100 rows of 100 fixed size items.
 *
 * @return The world
 */
static ecs_world_t* bench_picking_layout_world(void)
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
    ecs_set(world, panel, Layout, {.direction = DirectionColumn, .spacing = 1});
    for (y = 0; y < BENCH_UI_SIDE; y ++) {
        ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, panel);
        ecs_set(world, row, Layout, {.direction = DirectionRow, .spacing = 1});
        for (x = 0; x < BENCH_UI_SIDE; x ++) {
            ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, row);
            ecs_set(world, e, LayoutItem, {.width = {SizeModeFixed, 3},
                .height = {SizeModeFixed, 2}});
            if ((x + y) % 2) {
                ecs_add(world, e, Interactive);
            }
        }
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

void Picking_moves_8_per_frame_10k_layout_ui(
    bench_t *b)
{
    // spatial_perf/picking_cost
    ecs_world_t *world = bench_picking_layout_world();
    int32_t i = 0, k;
    while (bench_iter(b)) {
        for (k = 0; k < 8; k ++) {
            float x = 340.0f + (float)(((i * 8 + k) * 37) % 600);
            float y = 160.0f + (float)(((i * 8 + k) * 23) % 400);
            box_input_mouse_move(world, x, y, false);
        }

        ecs_progress(world, 0);
        i ++;
    }

    ecs_fini(world);
}

void Picking_wheel_4_per_frame_10k_layout_ui(
    bench_t *b)
{
    // spatial_perf/picking_cost
    ecs_world_t *world = bench_picking_layout_world();
    int32_t i = 0, k;
    while (bench_iter(b)) {
        box_input_mouse_move(world, 340.0f + (float)((i * 37) % 600),
            160.0f + (float)((i * 23) % 400), false);
        for (k = 0; k < 4; k ++) {
            box_input_mouse_scroll(world, 0, 1);
        }

        ecs_progress(world, 0);
        i ++;
    }

    ecs_fini(world);
}

void Picking_move_over_hires_mesh(
    bench_t *b)
{
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 3});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t ball = ecs_new(world);
    ecs_set(world, ball, Position, {0, 0, 0});
    ecs_set(world, ball, Sphere, {.segments = 256, .radius = 1});
    ecs_add(world, ball, Interactive);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    int32_t i = 0, hits = 0;
    while (bench_iter(b)) {
        box_input_mouse_move(world, 440.0f + (float)((i * 37) % 400),
            160.0f + (float)((i * 23) % 400), false);
        ecs_progress(world, 0);
        hits += ecs_singleton_get(world, PickStatus)->target == ball;
        i ++;
    }

    bench_counter(b, "hits", hits);
    ecs_fini(world);
}

void Picking_first_hover_dense_mesh(
    bench_t *b)
{
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 3});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    int32_t i = 0, f, hits = 0;
    while (bench_iter(b)) {
        bench_pause(b);
        ecs_entity_t ball = ecs_new(world);
        ecs_set(world, ball, Position, {0, 0, 0});
        ecs_set(world, ball, Sphere, {.segments = 192 + i % 64, .radius = 1});
        box_input_mouse_move(world, 5, 5, false);
        ecs_progress(world, 0);
        bench_resume(b);
        box_input_mouse_move(world, 640, 360, false);
        ecs_progress(world, 0);
        bench_pause(b);
        hits += ecs_singleton_get(world, PickStatus)->hit == ball;
        ecs_delete(world, ball);
        for (f = 0; f < 610; f ++) {
            ecs_progress(world, 0);
        }

        bench_resume(b);
        i ++;
    }

    bench_counter(b, "hits", hits);
    ecs_fini(world);
}

/** Rows and columns of the wave of critters */
#define BENCH_WAVE_SIDE (40)

void Picking_still_cursor_moving_wave_1600(
    bench_t *b)
{
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 30, 30});
    ecs_set(world, camera, Rotation, {-0.785f, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 50, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t ground = ecs_new(world);
    ecs_set(world, ground, Position, {0, -0.5f, 0});
    ecs_set(world, ground, Box, {400, 1, 400});

    /* A critter of 8 parts, none of them Interactive */
    ecs_entity_t critter = ecs_new_w_id(world, EcsPrefab);
    int32_t i;
    for (i = 0; i < 8; i ++) {
        ecs_entity_t part = ecs_new_w_pair(world, EcsChildOf, critter);
        ecs_set(world, part, Position, {(float)(i % 2) * 0.4f - 0.2f, (float)(i / 2) * 0.3f,
            0});
        ecs_set(world, part, Box, {0.3f, 0.3f, 0.3f});
    }

    /* A wave around the point under the cursor, every critter turning every frame */
    for (i = 0; i < BENCH_WAVE_SIDE * BENCH_WAVE_SIDE; i ++) {
        ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, critter);
        ecs_set(world, e, Position, {(float)(i % BENCH_WAVE_SIDE) * 1.5f - 30.0f, 0,
            (float)(i / BENCH_WAVE_SIDE) * 1.5f - 30.0f});
        ecs_set(world, e, Rotation, {0, 0, 0});
        ecs_set(world, e, AngularVelocity, {0, 1, 0});
    }

    /* A HUD with one button */
    ecs_entity_t hud = ecs_new(world);
    ecs_set(world, hud, Scene, {0});
    ecs_entity_t hud_camera = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, hud_camera, Position, {0, 0, 100});
    ecs_set(world, hud_camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t hud_view = ecs_new(world);
    ecs_set(world, hud_view, View, {.camera = hud_camera});
    ecs_entity_t button = ecs_new_w_pair(world, EcsChildOf, hud);
    ecs_set(world, button, Position, {-500, 300, 0});
    ecs_set(world, button, Rect, {80, 40});
    ecs_add(world, button, Interactive);

    ecs_entity_t system = ecs_lookup(world, "box.picking.PickingUpdate");
    ecs_measure_system_time(world, true);
    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0.016f);
    ecs_progress(world, 0.016f);
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    ecs_ftime_t start = s ? s->time_spent : 0;
    int32_t frames = 0;
    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
        frames ++;
    }

    s = system ? ecs_system_get(world, system) : NULL;
    bench_counter(b, "picking_ms_per_frame",
        s && frames ? (double)(s->time_spent - start) * 1000.0 / frames : 0);
    ecs_fini(world);
}
