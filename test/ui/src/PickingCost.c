/**
 * @file PickingCost.c
 * @brief Picking stays exact while it does less work: moves within a frame, UI elements without
 * a shape found through the spatial index, and meshes with many triangles.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Disable the entity in the context while the left mouse button is held, as an action
 * handler that closes a popup on any click would.
 *
 * @param it The iterator of the system
 */
static void DisableOnPress(
    ecs_iter_t *it)
{
    const Input *in = ecs_singleton_get(it->world, Input);
    ecs_entity_t e = *(ecs_entity_t*)it->ctx;
    if (in && in->keys[KeyMouseLeft].down && !ecs_has_id(it->world, e, EcsDisabled)) {
        ecs_enable(it->world, e, false);
    }
}

void PickingCost_moves_in_one_frame_hover_last(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut enters: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  a {\n"
        "    Position: {-100, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "  b {\n"
        "    Position: {100, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t enters; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t a = ecs_lookup(world, "hud.a");
    ecs_entity_t b = ecs_lookup(world, "hud.b");
    ecs_progress(world, 0);

    box_input_mouse_move(world, 540, 360, false);
    box_input_mouse_move(world, 640, 360, false);
    box_input_mouse_move(world, 735, 355, false);
    box_input_mouse_move(world, 745, 362, false);
    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, b);
    test_flt(status->cursor[0], 745);
    test_flt(status->cursor[1], 362);
    test_bool(ecs_has(world, b, Hovered), true);
    test_bool(ecs_has(world, a, Hovered), false);
    test_int(((const Mut*)ecs_get_id(world, b, mut))->enters, 1);

    box_input_mouse_move(world, 740, 360, false);
    box_input_mouse_move(world, 545, 360, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, a);
    test_bool(ecs_has(world, a, Hovered), true);
    test_bool(ecs_has(world, b, Hovered), false);
    test_int(((const Mut*)ecs_get_id(world, a, mut))->enters, 1);

    ecs_fini(world);
}

void PickingCost_drag_moves_in_one_frame_keep_delta(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut presses: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut dx: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.press(this)\n"
        "      presses = presses + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.drag(this)\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  slider {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {200, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t presses, releases; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_progress(world, 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move [600, 360]", NULL));
    ecs_os_free(box_terminal_run(world, s, "mouse.button left true", NULL));
    ecs_progress(world, 0);
    box_input_mouse_move(world, 610, 360, false);
    box_input_mouse_move(world, 625, 361, false);
    box_input_mouse_move(world, 650, 362, false);
    box_input_mouse_move(world, 690, 360, false);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, slider, mut);
    test_int(m->presses, 1);
    test_flt(m->dx, 90);
    test_flt(ecs_singleton_get(world, PickStatus)->drag[0], 90);
    test_bool(ecs_has(world, slider, Pressed), true);

    ecs_os_free(box_terminal_run(world, s, "mouse.button left false", NULL));
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    test_int(m->releases, 1);
    test_int(m->presses, 1);
    test_bool(ecs_has(world, slider, Pressed), false);

    ecs_fini(world);
}

void PickingCost_layout_element_follows_resize(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.layout\n"
        "using box.picking\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  menu {\n"
        "    Position: {0, 0, 0}\n"
        "    Layout: {direction: Row}\n"
        "    item {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
        "      Interactive\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t item = ecs_lookup(world, "hud.menu.item");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_input_mouse_move(world, 640 + 80, 360, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);

    ecs_set(world, item, LayoutItem, {.width = {SizeModeFixed, 200},
        .height = {SizeModeFixed, 40}});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, item, LayoutBounds)->width, 200);
    test_uint(ecs_singleton_get(world, PickStatus)->target, item);

    box_input_mouse_move(world, 640, 360 + 30, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);

    ecs_fini(world);
}

void PickingCost_text_bounds_element(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.camera\n"
        "using box.view\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  label {\n"
        "    Position: {50, 0, 0}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t label = ecs_lookup(world, "hud.label");
    ecs_add(world, label, Interactive);
    ecs_set(world, label, TextBounds, {100, 20});
    ecs_progress(world, 0);

    box_input_mouse_move(world, 640 + 90, 360 + 5, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, label);

    ecs_set(world, label, TextBounds, {60, 20});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);

    box_input_mouse_move(world, 640 + 70, 360 + 5, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, label);

    ecs_fini(world);
}

void PickingCost_disabled_layout_element(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.layout\n"
        "using box.picking\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  menu {\n"
        "    Position: {0, 0, 0}\n"
        "    Layout: {direction: Row, padding: {10, 10, 10, 10}}\n"
        "    Interactive\n"
        "    item {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t menu = ecs_lookup(world, "hud.menu");
    ecs_entity_t item = ecs_lookup(world, "hud.menu.item");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, item);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, menu);

    ecs_enable(world, item, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, menu);

    ecs_fini(world);
}

void PickingCost_dense_mesh_hits_surface(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 100, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t can = ecs_new(world);
    ecs_set(world, can, Position, {0, 0, 0});
    ecs_set(world, can, Cylinder, {.segments = 256, .length = 2, .radius = 1.5f});
    ecs_progress(world, 0);

    int32_t xi, yi;
    for (yi = -2; yi <= 2; yi ++) {
        for (xi = -8; xi <= 8; xi ++) {
            float x = (float)xi * 0.2f, y = (float)yi * 0.3f;
            box_input_mouse_move(world, 640 + x * 100, 360 - y * 100, false);
            ecs_progress(world, 0);
            const PickStatus *status = ecs_singleton_get(world, PickStatus);
            if (fabsf(x) >= 1.5f) {
                test_uint(status->hit, 0);
                continue;
            }

            float z = sqrtf(1.5f * 1.5f - x * x);
            test_uint(status->hit, can);
            test_assert(fabsf(status->point[0] - x) < 1e-3f);
            test_assert(fabsf(status->point[1] - y) < 1e-3f);
            test_assert(fabsf(status->point[2] - z) < 2e-3f);
            test_assert(fabsf(status->distance - (9.9f - z)) < 2e-3f);
        }
    }

    ecs_fini(world);
}

void PickingCost_disabled_on_press_not_targeted(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.camera\n"
        "using box.view\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  popup {\n"
        "    Position: {0, 0, 0}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t popup = ecs_lookup(world, "hud.popup");
    ecs_add(world, popup, Interactive);
    ecs_set(world, popup, TextBounds, {100, 40});
    ecs_system(world, {
        .phase = EcsPostLoad,
        .callback = DisableOnPress,
        .ctx = &popup,
        .immediate = true
    });
    ecs_progress(world, 0);
    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, popup);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.button left true", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_has_id(world, popup, EcsDisabled), true);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->pressed, 0);
    test_bool(ecs_has(world, popup, Pressed), false);

    ecs_fini(world);
}

void PickingCost_ui_wins_tie_with_shape(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  slab {\n"
        "    Position: {0, 0, 0}\n"
        "    Box: {80, 80, 0}\n"
        "    Interactive\n"
        "  }\n"
        "  label {\n"
        "    Position: {0, 0, 0}\n"
        "    Interactive\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t slab = ecs_lookup(world, "hud.slab");
    ecs_entity_t label = ecs_lookup(world, "hud.label");
    ecs_set(world, label, TextBounds, {40, 20});
    ecs_progress(world, 0);

    box_input_mouse_move(world, 640 + 10, 360, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, label);

    box_input_mouse_move(world, 640 + 30, 360 + 25, false);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, slab);

    ecs_fini(world);
}

void PickingCost_moves_around_wheel_and_key(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut moves: i32 = 0\n"
        "  mut scroll_x: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.move(this)\n"
        "      moves = moves + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.scroll(this)\n"
        "      scroll_x = e.screen_x\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  a {\n"
        "    Position: {-65, 0, 0}\n"
        "    Rect: {60, 200}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "  b {\n"
        "    Position: {30, 0, 0}\n"
        "    Rect: {130, 200}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t moves; float scroll_x; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t a = ecs_lookup(world, "hud.a");
    ecs_entity_t b = ecs_lookup(world, "hud.b");
    ecs_progress(world, 0);
    box_input_mouse_move(world, 590, 360, false);
    ecs_progress(world, 0);
    int32_t a_moves = ((const Mut*)ecs_get_id(world, a, mut))->moves;
    int32_t b_moves = ((const Mut*)ecs_get_id(world, b, mut))->moves;

    box_input_mouse_move(world, 600, 360, false);
    box_input_mouse_scroll(world, 0, 1);
    box_input_mouse_move(world, 610, 360, false);
    box_input_mouse_move(world, 620, 360, false);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, a, mut))->moves, a_moves + 1);
    test_int(((const Mut*)ecs_get_id(world, b, mut))->moves, b_moves + 1);
    test_flt(((const Mut*)ecs_get_id(world, a, mut))->scroll_x, 600);
    test_uint(ecs_singleton_get(world, PickStatus)->target, b);

    box_input_mouse_release(world);
    ecs_progress(world, 0);
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 595, 360 } });
    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyDown, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 640, 360 } });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 650, 360 } });
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, a, mut))->moves, a_moves + 2);
    test_int(((const Mut*)ecs_get_id(world, b, mut))->moves, b_moves + 2);
    test_flt(ecs_singleton_get(world, PickStatus)->cursor[0], 650);
    test_uint(ecs_singleton_get(world, PickStatus)->target, b);

    box_input_platform_push(world, &(InputEvent){ .kind = InputKeyUp, .key = KeyCtrlLeft });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 600, 360 } });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 640, 360 } });
    box_input_platform_push(world, &(InputEvent){ .kind = InputMove, .value = { 650, 360 } });
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, a, mut))->moves, a_moves + 2);
    test_int(((const Mut*)ecs_get_id(world, b, mut))->moves, b_moves + 3);

    ecs_fini(world);
}

void PickingCost_dense_mesh_changed_after_pick(void) {
    // spatial_perf/picking_cost
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 10});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 100, .near_ = 0.1f,
        .far_ = 100});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t can = ecs_new(world);
    ecs_set(world, can, Position, {0, 0, 0});
    ecs_set(world, can, Cylinder, {.segments = 256, .length = 2, .radius = 1.5f});
    ecs_progress(world, 0);

    int32_t f, step;
    float radius[3] = { 1.5f, 1.0f, 1.0f };
    int32_t segments[3] = { 256, 256, 200 };
    for (step = 0; step < 3; step ++) {
        ecs_set(world, can, Cylinder, {.segments = segments[step], .length = 2,
            .radius = radius[step]});
        for (f = 0; f < 20; f ++) {
            float x = f % 2 ? 0.6f : 0.5f;
            float z = sqrtf(radius[step] * radius[step] - x * x);
            box_input_mouse_move(world, 640 + x * 100, 360, false);
            ecs_progress(world, 0);
            const PickStatus *status = ecs_singleton_get(world, PickStatus);
            test_uint(status->hit, can);
            test_assert(fabsf(status->point[0] - x) < 1e-3f);
            test_assert(fabsf(status->point[2] - z) < 2e-3f);
        }
    }

    ecs_fini(world);
}
