/**
 * @file Follow.c
 * @brief Tests for the follow camera: the offset, the glide and the zoom presets.
 */

#include <box.h>
#include <bake_test.h>

static ecs_world_t *w;
static ecs_entity_t camera;
static ecs_entity_t player;

void Follow_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxUi);
    ECS_IMPORT(w, BoxController);
    player = ecs_entity(w, { .name = "player" });
    ecs_set(w, player, Position, {0, 0, 0});
    camera = ecs_entity(w, { .name = "camera" });

    Surface *surface = ecs_singleton_ensure(w, Surface);
    surface->width = 640;
    surface->height = 360;
}

void Follow_teardown(void)
{
    ecs_fini(w);
}

void Follow_the_module_binds_the_zoom_actions(void)
{
    // ore_engine/follow_camera
    ecs_entity_t zoom_in = ecs_lookup(w, "actions.ZoomIn");
    ecs_entity_t zoom_out = ecs_lookup(w, "actions.ZoomOut");
    test_assert(zoom_in != 0);
    test_assert(zoom_out != 0);
    test_str(ecs_get(w, zoom_in, InputAction)->description,
        "Step the follow camera one preset closer");

    uint64_t masks[4];
    test_int(box_input_bindings(w, zoom_in, masks, 4), 1);
    test_uint(masks[0], BoxKeyEqual);
    test_int(box_input_bindings(w, zoom_out, masks, 4), 1);
    test_uint(masks[0], BoxKeyMinus);
}

void Follow_the_camera_sits_at_the_offset_of_its_target(void)
{
    // ore_engine/follow_camera
    ecs_set(w, player, Position, {5, 0, 3});
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .offset = {0, 6, -10},
        .look_offset = {0, 1, 0}
    });

    ecs_progress(w, 0.1f);

    const Position *p = ecs_get(w, camera, Position);
    test_flt(p->x, 5);
    test_flt(p->y, 6);
    test_flt(p->z, -7);

    const LookAt *look = ecs_get(w, camera, LookAt);
    test_flt(look->x, 5);
    test_flt(look->y, 1);
    test_flt(look->z, 3);
    test_bool(ecs_get(w, camera, FollowCamera)->placed, true);
}

void Follow_the_camera_follows_a_target_that_moves(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .offset = {0, 5, -8},
        .glide = 20
    });

    ecs_progress(w, 0.1f);
    ecs_set(w, player, Position, {12, 0, 4});

    int32_t i;
    for (i = 0; i < 20; i ++) {
        ecs_progress(w, 0.1f);
    }

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->x > 11.9f);
    test_assert(p->z > -4.1f);
    test_assert(p->z < -3.9f);
}

void Follow_the_glide_lags_behind_a_target_that_jumps(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .offset = {0, 0, -10},
        .glide = 2
    });

    ecs_progress(w, 0.1f);
    test_flt(ecs_get(w, camera, Position)->x, 0);

    ecs_set(w, player, Position, {100, 0, 0});
    ecs_progress(w, 0.1f);

    const Position *p = ecs_get(w, camera, Position);
    test_assert(p->x > 1.0f);
    test_assert(p->x < 50.0f);
}

void Follow_a_camera_without_a_target_is_left_alone(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, { .offset = {0, 6, -10} });
    ecs_set(w, camera, Position, {1, 2, 3});

    ecs_progress(w, 0.1f);

    const Position *p = ecs_get(w, camera, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    test_bool(ecs_get(w, camera, FollowCamera)->placed, false);
}

void Follow_a_preset_replaces_the_offset_of_the_controller(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .offset = {0, 1, -1},
        .levels = {
            { .offset = {0, 4, -6}, .look_offset = {0, 1, 0} },
            { .offset = {0, 8, -12}, .look_offset = {0, 2, 0} }
        }
    });

    ecs_progress(w, 0.1f);

    const Position *p = ecs_get(w, camera, Position);
    test_flt(p->y, 4);
    test_flt(p->z, -6);
    test_flt(ecs_get(w, camera, LookAt)->y, 1);
}

void Follow_the_wheel_steps_to_the_next_preset(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .glide = 100,
        .levels = {
            { .offset = {0, 4, -6} },
            { .offset = {0, 8, -12} },
            { .offset = {0, 12, -18} }
        }
    });

    ecs_progress(w, 0.1f);
    test_int(ecs_get(w, camera, FollowCamera)->level, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputScroll, .value = {0, -1.0f} });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 1);

    box_input_inject(w, &(InputEvent){
        .kind = InputScroll, .value = {0, 1.0f} });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 0);
}

void Follow_the_zoom_actions_step_the_preset(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .levels = {
            { .offset = {0, 4, -6} },
            { .offset = {0, 8, -12} },
            { .offset = {0, 12, -18} }
        }
    });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyMinus, .duration = 1 });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 1);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyEqual, .duration = 1 });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 0);
}

void Follow_a_held_zoom_action_steps_once(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .levels = {
            { .offset = {0, 4, -6} },
            { .offset = {0, 8, -12} },
            { .offset = {0, 12, -18} }
        }
    });

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyMinus });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(w, 0.1f);
    }

    test_int(ecs_get(w, camera, FollowCamera)->level, 1);
}

void Follow_the_level_clamps_to_the_presets_that_are_authored(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .level = 7,
        .levels = {
            { .offset = {0, 4, -6} },
            { .offset = {0, 8, -12} }
        }
    });

    ecs_progress(w, 0.1f);
    test_int(ecs_get(w, camera, FollowCamera)->level, 1);
    test_flt(ecs_get(w, camera, Position)->y, 8);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputScroll, .value = {0, 1.0f} });
        ecs_progress(w, 0.1f);
    }

    test_int(ecs_get(w, camera, FollowCamera)->level, 0);
}

void Follow_a_camera_without_presets_keeps_its_level_at_zero(void)
{
    // ore_engine/follow_camera
    ecs_set(w, camera, FollowCamera, {
        .target = player, .offset = {0, 3, -5}, .level = 2 });

    box_input_inject(w, &(InputEvent){
        .kind = InputScroll, .value = {0, -1.0f} });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 0);
    test_flt(ecs_get(w, camera, Position)->y, 3);
}

void Follow_the_wheel_is_ignored_while_the_ui_holds_the_mouse(void)
{
    // ore_engine/follow_camera
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .levels = {
            { .offset = {0, 4, -6} },
            { .offset = {0, 8, -12} }
        }
    });

    ecs_entity_t root = ecs_new(w);
    ecs_add(w, root, Ui);
    ecs_get_mut(w, root, Ui)->height = 540;
    ecs_modified(w, root, Ui);
    ecs_set(w, ecs_new_w_pair(w, EcsChildOf, root), Rect, {120, 60});
    ecs_progress(w, 0.1f);
    box_input_inject(w, &(InputEvent){
        .kind = InputMouseMove, .value = {480, 270} });
    ecs_progress(w, 0.1f);
    test_bool(box_ui_mouse_captured(w), true);

    box_input_inject(w, &(InputEvent){
        .kind = InputScroll, .value = {0, -1.0f} });
    ecs_progress(w, 0.1f);

    test_int(ecs_get(w, camera, FollowCamera)->level, 0);
    test_bool(box_ui_mouse_captured(w), true);
}

void Follow_the_terrain_keeps_the_camera_above_its_bounds(void)
{
    // ore_engine/follow_camera
    ecs_entity_t terrain = ecs_entity(w, { .name = "terrain" });
    ecs_set(w, terrain, AABB, { .min = {-20, -1, -20}, .max = {20, 0.5f, 20} });
    ecs_set(w, camera, FollowCamera, {
        .target = player,
        .offset = {0, -4, -8},
        .terrain = terrain,
        .clearance = 2,
        .no_collision = true
    });

    ecs_progress(w, 0.1f);

    test_flt(ecs_get(w, camera, Position)->y, 2.5f);
}
