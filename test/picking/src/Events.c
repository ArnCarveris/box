/**
 * @file Events.c
 * @brief Tests for the pick events a script task awaits and for how they bubble.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/picking/picking_internal.h"

static ecs_world_t *w;
static ecs_entity_t fired_tag;
static ecs_entity_t leaf_tag;

void Events_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxPicking);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 100;
    s->height = 100;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60, .aspect_ratio = 1});
    fired_tag = ecs_entity(w, { .name = "Fired" });
    leaf_tag = ecs_entity(w, { .name = "LeafFired" });
}

void Events_teardown(void)
{
    ecs_fini(w);
}

void Events_hover_resumes_a_handler(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    test_assert(ball != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_a_waiter_registered_while_already_hovered_resumes_on_the_next_update(void)
{
    // picking/hover_race
    ecs_entity_t ball = ecs_entity(w, { .name = "ball" });
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, Hovered), true);

    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, Hovered), true);
    test_bool(ecs_has_id(w, ball, fired_tag), false);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_the_hovered_tag_follows_the_cursor(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, Hovered), true);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, Hovered), false);
}

void Events_press_resumes_a_handler(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.press(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_release_resumes_a_handler(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.release(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), false);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_click_resumes_a_handler(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), false);

    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_drag_resumes_a_handler(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.drag(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 3 });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), false);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {70, 50} });
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), true);
}

void Events_a_drag_does_not_raise_a_click(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.click(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 2 });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {70, 50} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, ball, fired_tag), false);
}

void Events_a_handler_receives_the_hit(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hx = 0\n"
        "  mut hz = 0\n"
        "  mut nz = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    const hit = await on.hover(this)\n"
        "    hx = hit.point.x\n"
        "    hz = hit.point.z\n"
        "    nz = hit.normal.z\n"
        "  }\n"
        "  probe {\n"
        "    Position: {hx, hz, nz}\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t probe = ecs_lookup(w, "ball.probe");
    test_assert(probe != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);

    const Position *p = ecs_get(w, probe, Position);
    test_assert(p != NULL);
    test_flt(p->x, 0);
    test_flt(p->y, -1);
    test_flt(p->z, -1);
}

void Events_an_event_bubbles_to_the_parent(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "using box.picking\n"
        "template Leaf {\n"
        "  Pickable\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "}\n"
        "template Holder {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "  Leaf leaf\n"
        "}\n"
        "Holder holder\n" });
    test_assert(s != 0);

    ecs_entity_t holder = ecs_lookup(w, "holder");
    ecs_entity_t leaf = ecs_lookup(w, "holder.leaf");
    test_assert(holder != 0);
    test_assert(leaf != 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, leaf, Hovered), true);
    test_bool(ecs_has_id(w, holder, fired_tag), true);
}

void Events_a_child_handler_consumes_the_event(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "using box.picking\n"
        "template Leaf {\n"
        "  mut hits = 0\n"
        "  Pickable\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    LeafFired\n"
        "  }\n"
        "}\n"
        "template Holder {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "  Leaf leaf\n"
        "}\n"
        "Holder holder\n" });
    test_assert(s != 0);

    ecs_entity_t holder = ecs_lookup(w, "holder");
    ecs_entity_t leaf = ecs_lookup(w, "holder.leaf");
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {10, 10} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);

    test_bool(ecs_has_id(w, leaf, leaf_tag), true);
    test_bool(ecs_has_id(w, holder, fired_tag), false);
}

void Events_a_still_cursor_runs_no_query(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_int(state->queries, 1);
    test_bool(ecs_has_id(w, ball, Hovered), true);

    ecs_progress(w, 0);
    test_int(state->queries, 0);
    test_bool(ecs_has_id(w, ball, Hovered), true);

    ecs_progress(w, 0);
    test_int(state->queries, 0);
}

void Events_every_view_costs_one_query(void)
{
    // picking/picking
    ecs_entity_t left = ecs_new(w);
    ecs_set(w, left, Position, {100, 0, 0});
    ecs_set(w, left, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_entity_t right = ecs_new(w);
    ecs_set(w, right, Position, {200, 0, 0});
    ecs_set(w, right, Camera, {.fov = 60, .aspect_ratio = 1});
    ecs_entity_t view_a = ecs_new(w);
    ecs_set(w, view_a, View, {.camera = left, .priority = 0, .enabled = true});
    ecs_entity_t view_b = ecs_new(w);
    ecs_set(w, view_b, View, {.camera = right, .priority = 1, .enabled = true});
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_int(state->queries, 2);
    test_bool(ecs_has_id(w, ball, Hovered), false);
}

void Events_disabled_picking_raises_nothing(void)
{
    // picking/picking
    ecs_entity_t ball = ecs_new(w);
    ecs_set(w, ball, Position, {0, 0, 0});
    ecs_set(w, ball, SphereCollider, {{0, 0, 0}, 1});
    PickSettings *settings = ecs_singleton_ensure(w, PickSettings);
    settings->enabled = false;
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {50, 50} });
    ecs_progress(w, 0);
    test_int(state->queries, 0);
    test_bool(ecs_has_id(w, ball, Hovered), false);
}

void Events_a_cancelled_task_drops_its_waiter(void)
{
    // picking/picking
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.collision\n"
        "template Target {\n"
        "  mut hits = 0\n"
        "  Position: {0, 0, 0}\n"
        "  SphereCollider: {radius: 1}\n"
        "  async {\n"
        "    await on.hover(this)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "}\n"
        "Target ball\n" });
    test_assert(s != 0);

    ecs_entity_t ball = ecs_lookup(w, "ball");
    ecs_progress(w, 0);

    PickState *state = box_picking_state(w);
    test_int(ecs_vec_count(&state->waiters), 1);

    ecs_delete(w, ball);
    ecs_progress(w, 0);
    test_int(ecs_vec_count(&state->waiters), 0);
}
