/**
 * @file Picking.c
 * @brief Picking driven by input commands: views front to back, bubbling, occlusion, textures.
 */

#include <box.h>
#include <math.h>
#include <bake_test.h>

void Picking_screen_ui_over_3d(void) {
    // ui/picking
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
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
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
        "cube {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {40, 20}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t cube = ecs_lookup(world, "cube");
    ecs_entity_t button = ecs_lookup(world, "hud.button");
    test_assert(mut != 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube, mut))->clicks, 0);

    ecs_os_free(box_command_run(world, s, "mouse_click left [690, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_stacked_views_fall_through(void) {
    // ui/picking
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
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
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
        "cube {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
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
        "    Rect: {300, 200}\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "menu {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  item {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "menu_view {\n"
        "  View: {camera: menu.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t cube = ecs_lookup(world, "cube");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t item = ecs_lookup(world, "menu.item");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 0);
    test_int(((const Mut*)ecs_get_id(world, item, mut))->clicks, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, cube);
    test_uint(status->view, ecs_lookup(world, "view"));
    test_uint(status->hit, panel);
    test_uint(status->hit_view, ecs_lookup(world, "hud_view"));

    ecs_os_free(box_command_run(world, s, "mouse_click left [940, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, item, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_reverse_view_order(void) {
    // ui/picking
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
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "first {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 100}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "second {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 100}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "first_view {\n"
        "  View: {camera: first.camera}\n"
        "}\n"
        "second_view {\n"
        "  View: {camera: second.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t first = ecs_lookup(world, "first.button");
    ecs_entity_t second = ecs_lookup(world, "second.button");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, second, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, first, mut))->clicks, 0);

    ecs_delete(world, ecs_lookup(world, "second_view"));
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = ecs_lookup(world, "second.camera")});
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, second, mut))->clicks, 2);
    test_int(((const Mut*)ecs_get_id(world, first, mut))->clicks, 0);

    ecs_delete(world, view);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, second, mut))->clicks, 2);
    test_int(((const Mut*)ecs_get_id(world, first, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_world_panel_occluded(void) {
    // ui/picking
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
        "panel {\n"
        "  Position: {0, 0, 0}\n"
        "  Scale: {0.01, 0.01, 0.01}\n"
        "  Rect: {400, 200}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "blocker {\n"
        "  Position: {-1, 0, 2}\n"
        "  Box: {2, 3, 1}\n"
        "}\n"
        "listener {\n"
        "  ScreenProbe: {}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t screen_mut = ecs_lookup(world, "ScreenProbe.mut");
    ecs_entity_t panel = ecs_lookup(world, "panel");
    ecs_entity_t listener = ecs_lookup(world, "listener");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [578, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 0);
    test_int(((const Mut*)ecs_get_id(world, listener, screen_mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "blocker"));

    ecs_os_free(box_command_run(world, s, "mouse_click left [702, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, listener, screen_mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_render_to_texture(void) {
    // ui/picking
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
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  mut x: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "      x = e.local_x\n"
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
        "  button {\n"
        "    Position: {-50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "    Interactive\n"
        "    Probe: {}\n"
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
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "quad {\n"
        "  Position: {0, 0, 0}\n"
        "  Quad: {4, 2}\n"
        "  PbrTextures: {albedo: screen_texture}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; float x; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [702, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "quad"));

    ecs_os_free(box_command_run(world, s, "mouse_click left [584, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 1);
    test_assert(m->x > 0 && m->x < 10);
    test_uint(ecs_singleton_get(world, PickStatus)->target, button);

    ecs_fini(world);
}

void Picking_render_to_texture_mapped(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    // The front face of the box spans x -2..2. Its UVs put the button (surface x 20..80 of 200)
    // at x -1.6..-0.4, MappingObject with a size of 2 repeats the surface every 2 units, with
    // the button at x -0.8..-0.2 and 1.2..1.8.
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.click(this)\n"
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
        "  button {\n"
        "    Position: {-50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "    Interactive\n"
        "    Probe: {}\n"
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
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "wall {\n"
        "  Position: {0, 0, -0.1}\n"
        "  Box: {4, 2, 0.2}\n"
        "  PbrTextures: {albedo: screen_texture}\n"
        "  TextureMapping: {mode: MappingObject, size: 2}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_progress(world, 0);

    // x -1.2: on the button through the UVs, not through the projection
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [565, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "wall"));

    // x 1.6: on the button through the projection only
    ecs_os_free(box_command_run(world, s, "mouse_click left [740, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->target, button);

    ecs_fini(world);
}

void Picking_render_to_texture_frame_mapped(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    // The wall spans x -2..2 in the world, a child of a TextureFrame at x 1.5 turned by 180
    // degrees around z. MappingFrame with a size of 2 projects 1.5 - x on the front face, so the
    // button (surface x 20..80 of 200) is at x -0.3..0.3 and 1.7..2, where MappingWorld would put
    // it at x -0.8..-0.2 and 1.2..1.8.
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.click(this)\n"
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
        "  button {\n"
        "    Position: {-50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "    Interactive\n"
        "    Probe: {}\n"
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
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "ship {\n"
        "  TextureFrame\n"
        "  Position: {1.5, 0, 0}\n"
        "  Rotation: {0, 0, 3.1415927}\n"
        "  wall {\n"
        "    Position: {1.5, 0, -0.1}\n"
        "    Box: {4, 2, 0.2}\n"
        "    PbrTextures: {albedo: screen_texture}\n"
        "    TextureMapping: {mode: MappingFrame, size: 2}\n"
        "  }\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_progress(world, 0);

    // x -0.5: on the button through MappingWorld, not through the frame
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [609, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "ship.wall"));

    // x 0: on the button through the frame only
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->target, button);

    ecs_fini(world);
}

void Picking_render_to_texture_sweep_mapped(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    // A square section swept along x from -3 over two lines of 1 and 5 units, its front wall at
    // z 0 (the profile x turns to -z), 62.5 window points per unit. The texture v runs along the
    // path: with MappingObject size 0.5 it is the distance along the path over 0.5, so the
    // surface repeats every half unit, where the fractions of the mesh would stretch it once
    // over the 6 units. The button covers the surface y 30..70.
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  mut x: f32 = 0\n"
        "  mut y: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "      x = e.local_x\n"
        "      y = e.local_y\n"
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
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 100, height: 100, texture: screen_texture, view_root: screen_views}\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "section_outline {\n"
        "  Path2D: {\n"
        "    start: {-0.5, -0.5},\n"
        "    segments: [\n"
        "      {kind: Line, end: {0.5, -0.5}},\n"
        "      {kind: Line, end: {0.5, 0.5}},\n"
        "      {kind: Line, end: {-0.5, 0.5}}\n"
        "    ],\n"
        "    closed: true\n"
        "  }\n"
        "}\n"
        "section { Profile2D: {outer: section_outline} }\n"
        "route {\n"
        "  Path3D: {\n"
        "    start: {-3, 0, -0.5},\n"
        "    segments: [\n"
        "      {kind: Line, end: {-2, 0, -0.5}},\n"
        "      {kind: Line, end: {3, 0, -0.5}}\n"
        "    ]\n"
        "  }\n"
        "}\n"
        "road {\n"
        "  Position: {0, 0, 0}\n"
        "  Sweep: {path: route, profile: section, scale: {1, 1}, rings: 48}\n"
        "  PbrTextures: {albedo: screen_texture}\n"
        "  TextureMapping: {mode: MappingObject, size: 0.5}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; float x; float y; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_progress(world, 0);

    // x 0, 3 units along the path: v 6 lands on the edge of the repeat, off the button, where
    // the fraction 0.5 of the mesh would land on its middle
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "road"));

    // x -1.744, 1.256 units along the path on the second line: v 2.512, the middle of the
    // button, where the fraction 0.209 of the mesh would miss it
    ecs_os_free(box_command_run(world, s, "mouse_click left [531, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 1);
    test_assert(fabsf(m->y) < 3.0f);
    test_uint(ecs_singleton_get(world, PickStatus)->target, button);

    // x -1.68, 1.32 units along the path: v 2.64, 14 points off the middle of the button
    ecs_os_free(box_command_run(world, s, "mouse_click left [535, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 2);
    test_assert(fabsf(fabsf(m->y) - 14.0f) < 3.0f);

    // x -2.752, 0.248 units along the path on the first line: v 0.496, the middle again
    ecs_os_free(box_command_run(world, s, "mouse_click left [468, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 3);
    test_assert(fabsf(m->y) < 3.0f);

    ecs_fini(world);
}

void Picking_render_to_texture_sweep_mapped_cap(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    // A square section of 1 unit scaled by 2 swept along +z from z -1 to 0, its end cap at z 0
    // facing the camera, 62.5 window points per unit. The cap maps the profile x and y; with
    // MappingObject size 0.5 its texture u is the profile x times the scale over 0.5, twice the
    // world x, where the mesh would map half of it. The button covers the surface x 30..70.
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut clicks: i32 = 0\n"
        "  mut x: f32 = 0\n"
        "  mut y: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "      x = e.local_x\n"
        "      y = e.local_y\n"
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
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {40, 100}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 100, height: 100, texture: screen_texture, view_root: screen_views}\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "section_outline {\n"
        "  Path2D: {\n"
        "    start: {-0.5, -0.5},\n"
        "    segments: [\n"
        "      {kind: Line, end: {0.5, -0.5}},\n"
        "      {kind: Line, end: {0.5, 0.5}},\n"
        "      {kind: Line, end: {-0.5, 0.5}}\n"
        "    ],\n"
        "    closed: true\n"
        "  }\n"
        "}\n"
        "section { Profile2D: {outer: section_outline} }\n"
        "route {\n"
        "  Path3D: {\n"
        "    start: {0, 0, -1},\n"
        "    segments: [\n"
        "      {kind: Line, end: {0, 0, -0.5}},\n"
        "      {kind: Line, end: {0, 0, 0}}\n"
        "    ]\n"
        "  }\n"
        "}\n"
        "post {\n"
        "  Position: {0, 0, 0}\n"
        "  Sweep: {path: route, profile: section, scale: {2, 2}}\n"
        "  PbrTextures: {albedo: screen_texture}\n"
        "  TextureMapping: {mode: MappingObject, size: 0.5}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; float x; float y; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_progress(world, 0);

    // x 0.496: u 0.992, on the edge of the repeat, off the button
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [671, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "post"));

    // x 0.256: u 0.512, the middle of the button, where the mesh would map 0.128 and miss it
    ecs_os_free(box_command_run(world, s, "mouse_click left [656, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 1);
    test_assert(fabsf(m->x) < 3.0f);
    test_uint(ecs_singleton_get(world, PickStatus)->target, button);

    // x 0.32: u 0.64, 14 points off the middle of the button
    ecs_os_free(box_command_run(world, s, "mouse_click left [660, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, button, mut);
    test_int(m->clicks, 2);
    test_assert(fabsf(fabsf(m->x) - 14.0f) < 3.0f);

    ecs_fini(world);
}

void Picking_nameplate(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.projection\n"
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
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "target {\n"
        "  Position: {2, 0, 0}\n"
        "  Box: {1, 1, 1}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  plate {\n"
        "    ProjectedAnchor: {target: target, view: view, offset: {0, 1, 0}}\n"
        "    Rect: {40, 16}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t plate = ecs_lookup(world, "hud.plate");
    ecs_entity_t target = ecs_lookup(world, "target");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [765, 298]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, plate, mut))->clicks, 1);

    ecs_set(world, target, Position, {-2, 0, 0});
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_click left [765, 298]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, plate, mut))->clicks, 1);

    ecs_os_free(box_command_run(world, s, "mouse_click left [515, 298]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, plate, mut))->clicks, 2);

    ecs_fini(world);
}

void Picking_bubbling(void) {
    // ui/picking
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
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "    button {\n"
        "      Position: {0, 0, 0}\n"
        "      Rect: {100, 40}\n"
        "      Interactive\n"
        "      Probe: {}\n"
        "      label {\n"
        "        Position: {0, 0, 0}\n"
        "        Rect: {50, 20}\n"
        "        Probe: {}\n"
        "      }\n"
        "    }\n"
        "    deco {\n"
        "      Position: {100, 0, 0}\n"
        "      Rect: {40, 40}\n"
        "      Probe: {}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    ecs_entity_t button = ecs_lookup(world, "hud.panel.button");
    ecs_entity_t label = ecs_lookup(world, "hud.panel.button.label");
    ecs_entity_t deco = ecs_lookup(world, "hud.panel.deco");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, label, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, button);

    ecs_os_free(box_command_run(world, s, "mouse_click left [740, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, deco, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 2);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);

    ecs_os_free(box_command_run(world, s, "mouse_click left [540, 300]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, panel, mut))->clicks, 3);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, label, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_hover_enter_leave(void) {
    // ui/picking
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
        "  mut leaves: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t enters; int32_t leaves; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "hud.button");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [200, 200]", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->enters, 0);
    test_bool(ecs_has(world, button, Hovered), false);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);

    ecs_os_free(box_command_run(world, s, "mouse_move [650, 365]", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->enters, 1);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->leaves, 0);
    test_bool(ecs_has(world, button, Hovered), true);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, button);
    test_uint(status->handler, button);
    test_uint(status->view, ecs_lookup(world, "hud_view"));
    test_flt(status->cursor[0], 650);
    test_flt(status->cursor[1], 365);

    ecs_os_free(box_command_run(world, s, "mouse_move [660, 370]", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->enters, 1);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->leaves, 0);

    ecs_os_free(box_command_run(world, s, "mouse_move [800, 360]", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->leaves, 1);
    test_bool(ecs_has(world, button, Hovered), false);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);

    ecs_fini(world);
}

void Picking_hover_follows_moving_element(void) {
    // ui/picking
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
        "  mut leaves: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t enters; int32_t leaves; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "hud.button");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [940, 160]", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->enters, 0);

    ecs_set(world, button, Position, {300, 200, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->enters, 1);
    test_bool(ecs_has(world, button, Hovered), true);

    ecs_set(world, button, Position, {0, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->leaves, 1);
    test_bool(ecs_has(world, button, Hovered), false);

    ecs_fini(world);
}

void Picking_press_drag_release(void) {
    // ui/picking
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut clicks: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
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

    typedef struct { int32_t presses, drags, releases, clicks; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, slider, mut);
    test_int(m->presses, 1);
    test_int(m->releases, 0);
    test_bool(ecs_has(world, slider, Pressed), true);
    test_uint(ecs_singleton_get(world, PickStatus)->pressed, slider);
    int32_t drags = m->drags;

    ecs_os_free(box_command_run(world, s, "mouse_sweep [600, 360] [680, 360] 4 4", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    test_assert(m->drags >= drags + 4);
    test_flt(m->dx, 80);
    test_int(m->releases, 0);
    test_bool(ecs_has(world, slider, Pressed), true);

    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    test_int(m->releases, 1);
    test_int(m->clicks, 1);
    test_int(m->presses, 1);
    test_bool(ecs_has(world, slider, Pressed), false);
    test_uint(ecs_singleton_get(world, PickStatus)->pressed, 0);

    ecs_fini(world);
}

void Picking_scroll(void) {
    // ui/picking
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
        "  mut dy: f32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      const e = await on.scroll(this)\n"
        "      scrolls = scrolls + 1\n"
        "      dy = e.delta_y\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  list {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {200, 300}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "    row {\n"
        "      Position: {0, 0, 0}\n"
        "      Rect: {180, 30}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t scrolls; float dy; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t list = ecs_lookup(world, "hud.list");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [100, 100]", NULL));
    ecs_os_free(box_command_run(world, s, "mouse_scroll 0 -2", NULL));
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, list, mut))->scrolls, 0);

    ecs_os_free(box_command_run(world, s, "mouse_move [640, 360]", NULL));
    ecs_os_free(box_command_run(world, s, "mouse_scroll 0 -3", NULL));
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, list, mut);
    test_int(m->scrolls, 1);
    test_flt(m->dy, -3);

    ecs_fini(world);
}

void Picking_layout_bounds(void) {
    // ui/picking
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
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  menu {\n"
        "    Position: {0, 0, 0}\n"
        "    Layout: {direction: Row, padding: {10, 10, 10, 10}, spacing: 10}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "    item {\n"
        "      LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
        "    }\n"
        "    dot {\n"
        "      Circle: {20}\n"
        "      Interactive\n"
        "      Probe: {}\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t menu = ecs_lookup(world, "hud.menu");
    ecs_entity_t dot = ecs_lookup(world, "hud.menu.dot");
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const LayoutBounds *b = ecs_get(world, menu, LayoutBounds);
    test_assert(b != NULL);
    test_flt(b->width, 170);
    test_flt(b->height, 60);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [565, 335]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, menu, mut))->clicks, 1);

    ecs_os_free(box_command_run(world, s, "mouse_click left [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, menu, mut))->clicks, 2);

    ecs_os_free(box_command_run(world, s, "mouse_click left [677, 343]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, dot, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, menu, mut))->clicks, 3);

    ecs_fini(world);
}

void Picking_box_shadow_not_picked(void) {
    // ui/box_shadow
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
        "  mut clicks: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.click(this)\n"
        "      clicks = clicks + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {40, 20}\n"
        "    BoxShadow: {offset_x: 30, offset_y: -30, blur: 20, spread: 10, "
            "color: {0, 0, 0, 255}}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t button = ecs_lookup(world, "hud.button");
    test_assert(mut != 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [670, 390]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);

    ecs_os_free(box_command_run(world, s, "mouse_click left [665, 375]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 0);

    ecs_os_free(box_command_run(world, s, "mouse_click left [650, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, button, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_unhandled_goes_to_screen(void) {
    // ui/picking
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
        "cube {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Probe: {}\n"
        "}\n"
        "listener {\n"
        "  ScreenProbe: {}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t cube = ecs_lookup(world, "cube");
    ecs_entity_t listener = ecs_lookup(world, "listener");
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube, ecs_lookup(world, "Probe.mut")))->clicks, 0);
    test_int(((const Mut*)ecs_get_id(
        world, listener, ecs_lookup(world, "ScreenProbe.mut")))->clicks, 1);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit, cube);
    test_uint(status->target, 0);
    test_bool(ecs_has(world, cube, Hovered), false);

    ecs_add(world, cube, Interactive);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube, ecs_lookup(world, "Probe.mut")))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(
        world, listener, ecs_lookup(world, "ScreenProbe.mut")))->clicks, 1);
    test_bool(ecs_has(world, cube, Hovered), true);

    ecs_fini(world);
}

void Picking_capture_drag_leaves_element(void) {
    // ui/pointer_capture
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut enters: i32 = 0\n"
        "  mut leaves: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
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
        "  other {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "" }) != 0);
    typedef struct { int32_t presses, drags, releases, enters, leaves; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_entity_t other = ecs_lookup(world, "hud.other");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->captured, slider);
    test_uint(status->pressed, slider);
    ecs_os_free(box_command_run(world, s, "mouse_sweep [600, 360] [960, 360] 6 6", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, slider, mut);
    const Mut *o = ecs_get_id(world, other, mut);
    status = ecs_singleton_get(world, PickStatus);
    test_assert(m->drags >= 6);
    test_flt(m->dx, 360);
    test_int(m->leaves, 0);
    test_int(m->releases, 0);
    test_bool(ecs_has(world, slider, Pressed), true);
    test_bool(ecs_has(world, slider, Hovered), true);
    test_bool(ecs_has(world, other, Hovered), false);
    test_uint(status->captured, slider);
    test_uint(status->target, slider);
    test_uint(status->hit, other);
    test_flt(status->drag[0], 360);
    test_flt(status->drag[1], 0);
    test_int(o->enters, 0);
    test_int(o->drags, 0);
    test_int(o->presses, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    test_int(m->releases, 1);

    ecs_fini(world);
}

void Picking_capture_drag_across_second_view(void) {
    // ui/pointer_capture
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut enters: i32 = 0\n"
        "  mut leaves: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
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
        "  other {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "front {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  cover {\n"
        "    Position: {200, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "front_view {\n"
        "  View: {camera: front.camera}\n"
        "}\n"
        "" }) != 0);
    typedef struct { int32_t presses, drags, releases, enters, leaves; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_entity_t other = ecs_lookup(world, "hud.other");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    ecs_entity_t cover = ecs_lookup(world, "front.cover");
    test_uint(ecs_singleton_get(world, PickStatus)->captured, slider);
    ecs_os_free(box_command_run(world, s, "mouse_sweep [600, 360] [800, 360] 4 4", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, slider, mut);
    const Mut *c = ecs_get_id(world, cover, mut);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_flt(m->dx, 200);
    test_int(m->leaves, 0);
    test_uint(status->target, slider);
    test_uint(status->view, ecs_lookup(world, "hud_view"));
    test_uint(status->hit, cover);
    test_uint(status->hit_view, ecs_lookup(world, "front_view"));
    test_bool(ecs_has(world, cover, Hovered), false);
    test_int(c->enters, 0);
    test_int(c->drags, 0);
    test_int(c->presses, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    c = ecs_get_id(world, cover, mut);
    test_int(m->releases, 1);
    test_int(c->releases, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [801, 360]", NULL));
    ecs_progress(world, 0);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->captured, 0);
    test_uint(status->target, cover);
    test_bool(ecs_has(world, cover, Hovered), true);

    ecs_fini(world);
}

void Picking_capture_release_outside(void) {
    // ui/pointer_capture
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut enters: i32 = 0\n"
        "  mut leaves: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
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
        "  other {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "" }) != 0);
    typedef struct { int32_t presses, drags, releases, enters, leaves; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_entity_t other = ecs_lookup(world, "hud.other");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [2000, 1500]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const Mut *m = ecs_get_id(world, slider, mut);
    test_assert(m->drags >= 1);
    test_flt(m->dx, 1400);
    test_bool(ecs_has(world, slider, Pressed), true);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, slider);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    m = ecs_get_id(world, slider, mut);
    test_int(m->releases, 1);
    test_bool(ecs_has(world, slider, Pressed), false);
    test_uint(ecs_singleton_get(world, PickStatus)->pressed, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, 0);

    ecs_fini(world);
}

void Picking_capture_cleared_after_release(void) {
    // ui/pointer_capture
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut enters: i32 = 0\n"
        "  mut leaves: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
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
        "  other {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "" }) != 0);
    typedef struct { int32_t presses, drags, releases, enters, leaves; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_entity_t other = ecs_lookup(world, "hud.other");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [940, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, 0);
    test_int(((const Mut*)ecs_get_id(world, slider, mut))->releases, 1);
    ecs_os_free(box_command_run(world, s, "mouse_move [942, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->target, other);
    test_bool(ecs_has(world, other, Hovered), true);
    test_bool(ecs_has(world, slider, Hovered), false);
    test_int(((const Mut*)ecs_get_id(world, other, mut))->enters, 1);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [940, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, other);
    test_int(((const Mut*)ecs_get_id(world, other, mut))->presses, 1);
    test_int(((const Mut*)ecs_get_id(world, slider, mut))->presses, 1);
    int32_t drags = ((const Mut*)ecs_get_id(world, slider, mut))->drags;
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, other);
    test_int(((const Mut*)ecs_get_id(world, slider, mut))->drags, drags);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);

    ecs_fini(world);
}

void Picking_capture_cleared_when_deleted(void) {
    // ui/pointer_capture
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
        "  mut drags: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  mut enters: i32 = 0\n"
        "  mut leaves: i32 = 0\n"
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
        "      drags = drags + 1\n"
        "      dx = dx + e.delta_x\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.release(this)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.enter(this)\n"
        "      enters = enters + 1\n"
        "      await on.leave(this)\n"
        "      leaves = leaves + 1\n"
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
        "  other {\n"
        "    Position: {300, 0, 0}\n"
        "    Rect: {40, 40}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "" }) != 0);
    typedef struct { int32_t presses, drags, releases, enters, leaves; float dx; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t slider = ecs_lookup(world, "hud.slider");
    ecs_entity_t other = ecs_lookup(world, "hud.other");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 360]", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left true", NULL));
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [650, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, slider);
    ecs_delete(world, slider);
    ecs_os_free(box_command_run(world, s, "mouse_move [700, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->captured, 0);
    test_uint(status->pressed, 0);
    test_uint(status->target, 0);
    ecs_os_free(box_command_run(world, s, "mouse_move [942, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->target, other);
    test_int(((const Mut*)ecs_get_id(world, other, mut))->drags, 0);
    ecs_os_free(box_command_run(world, s, "mouse_button left false", NULL));
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, 0);

    ecs_fini(world);
}

void Picking_focused_surface_views(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
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
        "cam_a {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "cam_b {\n"
        "  Position: {100, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "cube_a {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "cube_b {\n"
        "  Position: {100, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "views_a {\n"
        "  view { View: {camera: cam_a} }\n"
        "}\n"
        "views_b {\n"
        "  view { View: {camera: cam_b} }\n"
        "}\n"
        "surface_a {\n"
        "  Surface: {width: 1280, height: 720, view_root: views_a}\n"
        "}\n"
        "surface_b {\n"
        "  Surface: {width: 1280, height: 720, view_root: views_b}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t cube_a = ecs_lookup(world, "cube_a");
    ecs_entity_t cube_b = ecs_lookup(world, "cube_b");
    ecs_entity_t surface_a = ecs_lookup(world, "surface_a");
    ecs_entity_t surface_b = ecs_lookup(world, "surface_b");
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputFocus, .code = 1, .surface = surface_b });
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, Input)->surface, surface_b);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube_b, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube_a, mut))->clicks, 0);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputFocus, .code = 1, .surface = surface_a });
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_click left [640, 360]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube_b, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube_a, mut))->clicks, 1);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputMove, .value = { 640, 360 }, .surface = surface_b });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyDown, .key = KeyMouseLeft, .surface = surface_b });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyUp, .key = KeyMouseLeft, .surface = surface_b });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube_b, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube_a, mut))->clicks, 1);

    ecs_fini(world);
}

void Picking_focus_click_without_move(void) {
    // input_fixes/multi_window_input
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
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
        "cam_a {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "cam_b {\n"
        "  Position: {100, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "cube_a {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "cube_b {\n"
        "  Position: {100, 0, 0}\n"
        "  Box: {2, 2, 2}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "views_a {\n"
        "  view { View: {camera: cam_a} }\n"
        "}\n"
        "views_b {\n"
        "  view { View: {camera: cam_b} }\n"
        "}\n"
        "surface_a {\n"
        "  Surface: {width: 1280, height: 720, view_root: views_a}\n"
        "}\n"
        "surface_b {\n"
        "  Surface: {width: 1280, height: 720, view_root: views_b}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t cube_a = ecs_lookup(world, "cube_a");
    ecs_entity_t cube_b = ecs_lookup(world, "cube_b");
    ecs_entity_t surface_a = ecs_lookup(world, "surface_a");
    ecs_entity_t surface_b = ecs_lookup(world, "surface_b");
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputFocus, .code = 1, .surface = surface_a });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputMove, .value = { 100, 50 }, .surface = surface_a });
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputMove, .value = { 640, 360 }, .surface = surface_b });
    ecs_progress(world, 0);

    box_input_platform_push(world, &(InputEvent){
        .kind = InputFocus, .code = 1, .surface = surface_b });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyDown, .key = KeyMouseLeft, .surface = surface_b });
    box_input_platform_push(world, &(InputEvent){
        .kind = InputKeyUp, .key = KeyMouseLeft, .surface = surface_b });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, cube_b, mut))->clicks, 1);
    test_int(((const Mut*)ecs_get_id(world, cube_a, mut))->clicks, 0);

    ecs_fini(world);
}

void Picking_image_slice_surface_corner(void) {
    // ui/picking
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    /* A surface of 200 x 100 points at 2 pixels per point with a 10 x 10 button in its top left
     * and bottom right corners, shown by an 800 x 400 HUD panel sliced with insets of 40 texels,
     * 20 points of the surface */
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
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
        "screen_texture {}\n"
        "screen_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  top_left {\n"
        "    Position: {-85, 35, 0}\n"
        "    Rect: {10, 10}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "  bottom_right {\n"
        "    Position: {85, -35, 0}\n"
        "    Rect: {10, 10}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 200, height: 100, pixel_ratio: 2, texture: screen_texture,\n"
        "    view_root: screen_views}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  panel {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {800, 400}\n"
        "    Image: {texture: screen_texture}\n"
        "    ImageSlice: {left: 40, top: 40, right: 40, bottom: 40}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    typedef struct { int32_t clicks; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t top_left = ecs_lookup(world, "screen_ui.top_left");
    ecs_entity_t bottom_right = ecs_lookup(world, "screen_ui.bottom_right");
    ecs_entity_t panel = ecs_lookup(world, "hud.panel");
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    const SurfaceStatus *st = ecs_get(world, ecs_lookup(world, "screen_surface"), SurfaceStatus);
    test_assert(st != NULL);
    test_int(st->width, 400);
    test_int(st->height, 200);

    /* 30, 30 points into the panel is 3 / 4 into its 40 point corner: surface point 15, 15, on
     * the top left button. Stretched it would be 7.5, 7.5 and with the insets in points 30, 30 */
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_click left [270, 190]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, panel);
    test_int(((const Mut*)ecs_get_id(world, top_left, mut))->clicks, 1);

    /* The bottom right corner maps the same way to surface point 185, 85 */
    ecs_os_free(box_command_run(world, s, "mouse_click left [1010, 530]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(((const Mut*)ecs_get_id(world, bottom_right, mut))->clicks, 1);

    /* Without the ImageSlice the panel stretches the surface and the click misses the button */
    ecs_remove(world, panel, ImageSlice);
    ecs_progress(world, 0);
    ecs_os_free(box_command_run(world, s, "mouse_click left [270, 190]", NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, panel);
    test_int(((const Mut*)ecs_get_id(world, top_left, mut))->clicks, 1);

    ecs_fini(world);
}
