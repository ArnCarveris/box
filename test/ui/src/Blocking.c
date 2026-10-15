/**
 * @file Blocking.c
 * @brief InputBlocker elements keep the pointer events that hit them from the views behind.
 */

#include <box.h>
#include <bake_test.h>

void Blocking_stops_pass_through(void) {
    // input_fixes/ui_blocks_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.layout\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "template ScreenProbe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(box.input.screen)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "ground {\n"
        "  Position: {0, 0, -5}\n"
        "  Box: {200, 200, 1}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "listener {\n"
        "  ScreenProbe: {}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {200, 100}\n"
        "    InputBlocker\n"
        "    label {\n"
        "      Position: {-60, 0, 1}\n"
        "      Rect: {40, 20}\n"
        "    }\n"
        "    button {\n"
        "      Position: {60, 0, 1}\n"
        "      Rect: {40, 20}\n"
        "      Interactive\n"
        "      Probe: {}\n"
        "    }\n"
        "  }\n"
        "  open_panel {\n"
        "    Position: {-400, 0, 0}\n"
        "    Rect: {150, 100}\n"
        "  }\n"
        "  menu {\n"
        "    Position: {400, 0, 0}\n"
        "    Layout: {direction: Row}\n"
        "    InputBlocker\n"
        "    item {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t screen_mut = ecs_lookup(world, "ScreenProbe.mut");
    ecs_entity_t ground = ecs_lookup(world, "ground");
    ecs_entity_t listener = ecs_lookup(world, "listener");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t label = ecs_lookup(world, "hud.panel.label");
    ecs_entity_t button = ecs_lookup(world, "hud.panel.button");
    ecs_entity_t menu = ecs_lookup(world, "hud.menu");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left [100, 600]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [640, 340]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, listener, screen_mut))->clicks, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, panel);
    test_uint(status->handler, panel);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [580, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, label);
    test_uint(status->handler, panel);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [700, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [1040, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, menu);

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [240, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 2);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ground);

    ecs_remove(world, panel, InputBlocker);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left [640, 340]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 3);

    ecs_fini(world);
}

void Blocking_press_stays_on_panel(void) {
    // input_fixes/ui_blocks_input
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
        "  async {\n"
        "    while true {\n"
        "      await on.press(this)\n"
        "      presses = presses + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "ground {\n"
        "  Position: {0, 0, -5}\n"
        "  Box: {200, 200, 1}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {200, 100}\n"
        "    InputBlocker\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t presses, releases; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t ground = ecs_lookup(world, "ground");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move [640, 360]", NULL));
    ecs_os_free(box_terminal_run(world, s, "mouse.button left true", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_has(world, panel, Pressed), true);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, panel);

    ecs_os_free(box_terminal_run(world, s, "mouse.sweep [640, 360] [100, 600] 4 4", NULL));
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    ecs_os_free(box_terminal_run(world, s, "mouse.button left false", NULL));
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, ground, mut);
    test_int(m->presses, 0);
    test_int(m->releases, 0);
    test_bool(ecs_has(world, ground, Pressed), false);
    test_bool(ecs_has(world, panel, Pressed), false);

    ecs_os_free(box_terminal_run(world, s, "mouse.button left true", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->presses, 1);

    ecs_fini(world);
}
