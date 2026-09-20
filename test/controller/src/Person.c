/**
 * @file Person.c
 * @brief Tests for the first person controller: walking, jumping and flying.
 */

#include <box.h>
#include <bake_test.h>

/** Seconds a frame takes, the fixed step of a headless run */
#define STEP (1.0f / 60.0f)

static ecs_world_t *w;
static ecs_entity_t person;

void Person_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    person = ecs_entity(w, { .name = "player" });
    ecs_add(w, person, FirstPersonController);
}

void Person_teardown(void)
{
    ecs_fini(w);
}

void Person_the_module_binds_the_character_actions(void)
{
    // controllers/first_person
    uint64_t masks[4];
    ecs_entity_t jump = ecs_lookup(w, "actions.Jump");
    ecs_entity_t crouch = ecs_lookup(w, "actions.Crouch");
    ecs_entity_t run = ecs_lookup(w, "actions.Run");
    ecs_entity_t fly = ecs_lookup(w, "actions.Fly");
    test_assert(jump != 0);
    test_assert(crouch != 0);
    test_assert(run != 0);
    test_assert(fly != 0);

    test_int(box_input_bindings(w, jump, masks, 4), 1);
    test_uint(masks[0], BoxKeySpace);
    test_int(box_input_bindings(w, crouch, masks, 4), 1);
    test_uint(masks[0], BoxKeyCtrl);
    test_int(box_input_bindings(w, run, masks, 4), 1);
    test_uint(masks[0], BoxKeyShift);
    test_int(box_input_bindings(w, fly, masks, 4), 1);
    test_uint(masks[0], BoxKeyF);
}

void Person_the_character_stands_at_eye_height(void)
{
    // controllers/first_person
    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);

    ecs_get_mut(w, person, FirstPersonController)->eye = 1.2f;
    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 1.2f);
}

void Person_gravity_pulls_the_character_down(void)
{
    // controllers/first_person
    ecs_set(w, person, Position, {0, 10, 0});

    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->y < 10.0f);
    test_assert(p->y > 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, false);

    for (i = 0; i < 100; i ++) {
        ecs_progress(w, STEP);
    }

    test_flt(ecs_get(w, person, Position)->y, 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_walking_moves_in_the_view_plane(void)
{
    // controllers/first_person
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z > 2.0f);
    test_flt(p->x, 0);
    test_flt(p->y, 1.7f);
}

void Person_running_is_faster_than_walking(void)
{
    // controllers/first_person
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    float walked = ecs_get(w, person, Position)->z;
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyShiftLeft });
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    float ran = ecs_get(w, person, Position)->z - walked;
    test_assert(ran > walked * 1.5f);
    test_assert(ecs_get(w, person, FirstPersonController)->velocity.z > 7.0f);
}

void Person_a_jump_rises_and_lands(void)
{
    // controllers/first_person
    ecs_progress(w, STEP);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeySpace, .duration = 1 });

    int32_t i;
    float top = 0;
    for (i = 0; i < 20; i ++) {
        ecs_progress(w, STEP);
        float y = ecs_get(w, person, Position)->y;
        if (y > top) {
            top = y;
        }
    }

    test_assert(top > 2.0f);
    test_assert(top < 2.5f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, false);

    for (i = 0; i < 40; i ++) {
        ecs_progress(w, STEP);
    }

    test_flt(ecs_get(w, person, Position)->y, 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_a_jump_needs_the_ground(void)
{
    // controllers/first_person
    ecs_progress(w, STEP);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeySpace, .duration = 1 });
    ecs_progress(w, STEP);
    ecs_progress(w, STEP);

    float speed = ecs_get(w, person, FirstPersonController)->velocity.y;
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeySpace, .duration = 1 });
    ecs_progress(w, STEP);

    test_assert(ecs_get(w, person, FirstPersonController)->velocity.y < speed);
}

void Person_crouching_lowers_the_eye(void)
{
    // controllers/first_person
    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 1.7f);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyCtrlLeft });
    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 0.9f);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyUp, .code = BoxKeyCtrlLeft });
    ecs_progress(w, STEP);
    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 1.7f);
}

void Person_the_fly_toggle_switches_to_free_flight(void)
{
    // controllers/first_person
    ecs_progress(w, STEP);
    test_bool(ecs_get(w, person, FirstPersonController)->flying, false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyF, .duration = 1 });
    ecs_progress(w, STEP);
    test_bool(ecs_get(w, person, FirstPersonController)->flying, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyE });
    int32_t i;
    for (i = 0; i < 30; i ++) {
        ecs_progress(w, STEP);
    }

    test_assert(ecs_get(w, person, Position)->y > 2.5f);

    box_input_reset(w);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyF, .duration = 1 });
    ecs_progress(w, STEP);
    test_bool(ecs_get(w, person, FirstPersonController)->flying, false);

    for (i = 0; i < 120; i ++) {
        ecs_progress(w, STEP);
    }

    test_flt(ecs_get(w, person, Position)->y, 1.7f);
}

void Person_the_terrain_sets_the_ground(void)
{
    // controllers/first_person
    ecs_entity_t terrain = ecs_entity(w, { .name = "ground" });
    ecs_set(w, terrain, AABB, { .min = {-5, -1, -5}, .max = {5, 3, 5} });
    ecs_set(w, person, FirstPersonController, { .terrain = terrain });

    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 4.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_the_ground_height_holds_the_character(void)
{
    // controllers/first_person
    ecs_set(w, person, FirstPersonController, { .ground = 2.5f });

    ecs_progress(w, STEP);
    test_flt(ecs_get(w, person, Position)->y, 4.2f);
}

void Person_the_controller_writes_orientation_and_lookat(void)
{
    // controllers/first_person
    ecs_add(w, person, LookAt);
    FirstPersonController *fp = ecs_get_mut(w, person, FirstPersonController);
    fp->yaw = 1.5707963f;
    ecs_progress(w, STEP);

    const LookAt *look = ecs_get(w, person, LookAt);
    test_flt(ecs_get(w, person, Orientation)->yaw, 1.5707963f);
    test_flt(look->x, 1.0f);
    test_flt(look->y, 1.7f);
    test_assert(look->z < 0.01f);
}

void Person_a_wall_stops_the_character(void)
{
    // collision/controller_collision
    ecs_entity_t wall = ecs_entity(w, { .name = "wall" });
    ecs_set(w, wall, AABB, { .min = {-10, 0, 2}, .max = {10, 3, 2.5f} });
    box_collision_update(w);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z > 1.5f);
    test_assert(p->z < 1.7f);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_walking_into_a_wall_slides_along_it(void)
{
    // collision/controller_collision
    ecs_entity_t wall = ecs_entity(w, { .name = "wall" });
    ecs_set(w, wall, AABB, { .min = {-10, 0, 2}, .max = {10, 3, 2.5f} });
    box_collision_update(w);

    ecs_get_mut(w, person, FirstPersonController)->yaw = 0.7853982f;
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z < 1.7f);
    test_assert(p->x > 1.5f);
    test_flt(p->y, 1.7f);
}

void Person_a_step_up_climbs_a_small_ledge(void)
{
    // collision/controller_collision
    ecs_entity_t ledge = ecs_entity(w, { .name = "ledge" });
    ecs_set(w, ledge, AABB, { .min = {-5, 0, 1}, .max = {5, 0.3f, 6} });
    box_collision_update(w);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z > 2.0f);
    test_assert(p->y > 1.95f);
    test_assert(p->y < 2.05f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_standing_on_geometry_holds_the_character(void)
{
    // collision/controller_collision
    ecs_entity_t platform = ecs_entity(w, { .name = "platform" });
    ecs_set(w, platform, AABB, { .min = {-2, 0, -2}, .max = {2, 2, 2} });
    ecs_set(w, person, Position, {0, 6, 0});
    box_collision_update(w);

    int32_t i;
    for (i = 0; i < 120; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->y > 3.65f);
    test_assert(p->y < 3.75f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
    test_flt(ecs_get(w, person, FirstPersonController)->velocity.y, 0);
}

void Person_falling_off_geometry_lands_on_the_ground(void)
{
    // collision/controller_collision
    ecs_entity_t platform = ecs_entity(w, { .name = "platform" });
    ecs_set(w, platform, AABB, { .min = {-2, 0, -2}, .max = {2, 2, 2} });
    ecs_set(w, person, Position, {0, 3.7f, 0});
    box_collision_update(w);

    ecs_progress(w, STEP);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 150; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z > 2.5f);
    test_flt(p->y, 1.7f);
    test_bool(ecs_get(w, person, FirstPersonController)->grounded, true);
}

void Person_disabling_collision_walks_through_geometry(void)
{
    // collision/controller_collision
    ecs_entity_t wall = ecs_entity(w, { .name = "wall" });
    ecs_set(w, wall, AABB, { .min = {-10, 0, 2}, .max = {10, 3, 2.5f} });
    ecs_get_mut(w, person, FirstPersonController)->no_collision = true;
    box_collision_update(w);

    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyW });
    int32_t i;
    for (i = 0; i < 60; i ++) {
        ecs_progress(w, STEP);
    }

    const Position *p = ecs_get(w, person, Position);
    test_assert(p->z > 2.5f);
    test_flt(p->y, 1.7f);
}
