/**
 * @file ActionTrigger.c
 * @brief ActionTrigger on UI rendered to a texture, picked through the UV of the mesh showing it.
 */

#include <box.h>
#include <bake_test.h>

void ActionTrigger_render_to_texture(void) {
    // ui/action_trigger
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
        "actions {\n"
        "  play { Action: {} }\n"
        "}\n"
        "template Counter {\n"
        "  mut presses: i32 = 0\n"
        "  mut releases: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.pressed(actions.play)\n"
        "      presses = presses + 1\n"
        "    }\n"
        "  }\n"
        "  async {\n"
        "    while true {\n"
        "      await on.released(actions.play)\n"
        "      releases = releases + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "counter { Counter: {} }\n"
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
        "    ActionTrigger: {action: actions.play}\n"
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

    typedef struct { int32_t presses, releases; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Counter.mut");
    ecs_entity_t counter = ecs_lookup(world, "counter");
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    ecs_entity_t play = ecs_lookup(world, "actions.play");
    int32_t i, presses = 0;
    ecs_progress(world, 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.click left [702, 360]", NULL));
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        presses += ecs_get(world, play, ActionState)->pressed;
    }

    test_int(presses, 0);
    test_uint(ecs_singleton_get(world, PickStatus)->hit, ecs_lookup(world, "quad"));

    ecs_os_free(box_terminal_run(world, s, "mouse.click left [584, 360]", NULL));
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0);
        presses += ecs_get(world, play, ActionState)->pressed;
    }

    test_int(presses, 1);
    test_uint(ecs_singleton_get(world, PickStatus)->handler, button);
    const Mut *m = ecs_get_id(world, counter, mut);
    test_int(m->presses, 1);
    test_int(m->releases, 1);

    ecs_fini(world);
}
