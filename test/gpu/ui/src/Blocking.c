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
        "      Item: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
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

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [100, 600]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);

    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 340]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, listener, screen_mut))->clicks, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, panel);
    test_uint(status->handler, panel);

    ecs_os_free(box_command_run(world, s, "mouse_click left [580, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, label);
    test_uint(status->handler, panel);

    ecs_os_free(box_command_run(world, s, "mouse_click left [700, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);

    ecs_os_free(box_command_run(world, s, "mouse_click left [1040, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, menu);

    ecs_os_free(box_command_run(world, s, "mouse_click left [240, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 2);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ground);

    ecs_remove(world, panel, InputBlocker);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 340]", NULL));
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

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [640, 360]", NULL));
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_has(world, panel, Pressed), true);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, panel);

    ecs_os_free(box_command_run(world, s, "mouse_sweep [640, 360] [100, 600] 4 4", NULL));
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
    }

    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, ground, mut);
    test_int(m->presses, 0);
    test_int(m->releases, 0);
    test_bool(ecs_has(world, ground, Pressed), false);
    test_bool(ecs_has(world, panel, Pressed), false);

    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->presses, 1);

    ecs_fini(world);
}

void Blocking_mouse_binding(void) {
    // input_fixes/ui_blocks_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "using box.input\n"
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
        "}\n"
        "Fire { Action: {}\n"
        "  Binding: {key: MouseLeft}\n"
        "}\n"
        "Jump { Action: {}\n"
        "  Binding: {key: Space}\n"
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

    ecs_entity_t fire = ecs_lookup(world, "Fire");
    ecs_entity_t jump = ecs_lookup(world, "Jump");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_input_mouse_click(world, KeyMouseLeft, (float[]){640, 360}, false, 2);
    box_input_key_press(world, KeySpace, 1);
    bool jumped = false;
    for (int32_t i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        const ActionState *f = ecs_get(world, fire, ActionState);
        test_bool(f->pressed, false);
        test_bool(f->down, false);
        test_bool(f->released, false);
        jumped = jumped || ecs_get(world, jump, ActionState)->pressed;
    }

    test_bool(jumped, true);

    box_input_mouse_click(world, KeyMouseLeft, (float[]){100, 600}, false, 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->released, true);

    box_input_mouse_move(world, 100, 600, false);
    box_input_key(world, KeyMouseLeft, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);
    box_input_mouse_sweep(world, &(box_input_sweep_t){
        .from = {100, 600}, .to = {640, 360}, .steps = 2, .frames = 2 });
    for (int32_t i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        test_bool(ecs_get(world, fire, ActionState)->down, true);
    }

    box_input_key(world, KeyMouseLeft, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->released, true);

    box_input_key(world, KeyMouseLeft, true);
    box_input_mouse_sweep(world, &(box_input_sweep_t){
        .from = {640, 360}, .to = {100, 600}, .steps = 2, .frames = 2 });
    for (int32_t i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        test_bool(ecs_get(world, fire, ActionState)->down, false);
    }

    box_input_key(world, KeyMouseLeft, false);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->released, false);

    ecs_remove(world, panel, InputBlocker);
    box_input_mouse_click(world, KeyMouseLeft, (float[]){640, 360}, false, 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);

    ecs_fini(world);
}

void Blocking_camera_look_and_zoom(void) {
    // input_fixes/ui_blocks_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.picking\n"
        "using box.controllers\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "  CameraController: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
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

    ecs_entity_t camera = ecs_lookup(world, "camera");
    ecs_progress(world, 0.1f);
    ecs_progress(world, 0.1f);
    Position p0 = *ecs_get(world, camera, Position);
    Rotation r0 = *ecs_get(world, camera, Rotation);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {640, 360}, .to = {900, 500}, .steps = 4, .frames = 4,
        .button = KeyMouseRight });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.1f);
    }

    box_input_mouse_move(world, 640, 360, false);
    box_input_mouse_scroll(world, 0, 3);
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.1f);
    }

    const Rotation *r = ecs_get(world, camera, Rotation);
    const Position *p = ecs_get(world, camera, Position);
    test_flt(r->x, r0.x);
    test_flt(r->y, r0.y);
    test_flt(p->x, p0.x);
    test_flt(p->y, p0.y);
    test_flt(p->z, p0.z);

    box_input_mouse_drag(world, &(box_input_sweep_t){
        .from = {100, 600}, .to = {360, 700}, .steps = 4, .frames = 4,
        .button = KeyMouseRight });
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.1f);
    }

    r = ecs_get(world, camera, Rotation);
    test_assert(r->y != r0.y);

    box_input_mouse_move(world, 100, 600, false);
    box_input_mouse_scroll(world, 0, 3);
    for (int32_t i = 0; i < 8; i ++) {
        ecs_progress(world, 0.1f);
    }

    p = ecs_get(world, camera, Position);
    test_assert(p->x != p0.x || p->y != p0.y || p->z != p0.z);

    ecs_fini(world);
}

void Blocking_hover_and_wheel(void) {
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
        "  mut scrolls: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.scroll(this)\n"
        "      scrolls = scrolls + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "template ScreenProbe {\n"
        "  mut scrolls: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.scroll(box.input.screen)\n"
        "      scrolls = scrolls + 1\n"
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
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t scrolls; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t screen_mut = ecs_lookup(world, "ScreenProbe.mut");
    ecs_entity_t ground = ecs_lookup(world, "ground");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t listener = ecs_lookup(world, "listener");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_input_mouse_move(world, 100, 600, false);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, ground, Hovered), true);

    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, ground, Hovered), false);
    test_bool(ecs_has(world, panel, Hovered), true);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, panel);

    box_input_mouse_scroll(world, 0, 1);
    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.scroll[1], 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->scrolls, 1);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->scrolls, 0);
    test_int(((const Mut*)ecs_get_id(world, listener, screen_mut))->scrolls, 0);

    box_input_mouse_move(world, 100, 600, false);
    ecs_progress(world, 0);
    test_bool(ecs_has(world, ground, Hovered), true);
    test_bool(ecs_has(world, panel, Hovered), false);

    box_input_mouse_scroll(world, 0, 1);
    ecs_progress(world, 0);
    test_flt(ecs_singleton_get(world, Input)->mouse.scroll[1], 1);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->scrolls, 1);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->scrolls, 1);

    ecs_fini(world);
}

void Blocking_render_to_texture(void) {
    // input_fixes/ui_blocks_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.picking\n"
        "using box.input\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "screen_texture {}\n"
        "screen_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  open {\n"
        "    Position: {-50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "  }\n"
        "  blocker {\n"
        "    Position: {50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "    InputBlocker\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 200, height: 100, texture: screen_texture, view_root: screen_views}\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "ground_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "ground {\n"
        "  Position: {0, 0, -5}\n"
        "  Box: {200, 200, 1}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "Fire { Action: {}\n"
        "  Binding: {key: MouseLeft}\n"
        "}\n"
        "panel_scene {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 10}\n"
        "    Camera: {}\n"
        "  }\n"
        "  quad {\n"
        "    Position: {0, 0, 0}\n"
        "    Quad: {4, 2}\n"
        "    PbrTextures: {albedo: screen_texture}\n"
        "  }\n"
        "}\n"
        "panel_view {\n"
        "  View: {camera: panel_scene.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t ground = ecs_lookup(world, "ground");
    ecs_entity_t blocker = ecs_lookup(world, "screen_ui.blocker");
    ecs_entity_t fire = ecs_lookup(world, "Fire");
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_input_mouse_click(world, KeyMouseLeft, (float[]){702, 360}, false, 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, false);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, blocker);

    box_input_mouse_click(world, KeyMouseLeft, (float[]){584, 360}, false, 1);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, fire, ActionState)->pressed, true);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, ground, mut))->clicks, 1);

    ecs_fini(world);
}
