/**
 * @file Gizmo.c
 * @brief Tests for the transform gizmos: what is drawn and what dragging a handle changes.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;
static ecs_entity_t rock;

void Gizmo_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 1280;
    s->height = 720;
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_entity(w, { .name = "main" });
    ecs_set(w, view, View, { .camera = cam, .enabled = true });
    ecs_entity_t scene = ecs_entity(w, { .name = "scene" });
    rock = ecs_entity(w, { .name = "rock", .parent = scene });
    ecs_set(w, rock, Position, {0, 0, 0});
    ecs_set(w, rock, Sphere, { .radius = 1 });
    box_editor_enable(w, true);
    ecs_progress(w, 0);
}

void Gizmo_teardown(void)
{
    ecs_fini(w);
}

void Gizmo_select_tool_draws_the_selection_box(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t asset = ecs_lookup(w, "box.editor.gizmos.SelectionEdge");
    test_assert(asset != 0);
    int32_t edges = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const DrawInstance *d = ecs_get(w, it.entities[i], DrawInstance);
            test_uint(d->asset, asset);
            test_bool(d->overlay, true);
            test_assert(!ecs_has_id(w, it.entities[i], Hidden));
            test_assert(ecs_has_id(w, it.entities[i], NoCollider));
            const Position *p = ecs_get(w, it.entities[i], Position);
            test_assert(fabsf(p->x) <= 1.01f && fabsf(p->y) <= 1.01f && fabsf(p->z) <= 1.01f);
            edges ++;
        }
    }

    test_int(edges, 12);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    edges = 0;
    it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_get(w, it.entities[i], DrawInstance)->asset == asset) {
                test_assert(!ecs_has_id(w, it.entities[i], Hidden));
                edges ++;
            }
        }
    }

    test_int(edges, 12);
}

void Gizmo_move_tool_draws_the_move_gizmo_at_the_selection(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t edge = ecs_lookup(w, "box.editor.gizmos.SelectionEdge");
    test_int(ecs_count(w, DrawInstance), 13);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_get(w, it.entities[i], DrawInstance)->asset != edge) {
                inst = it.entities[i];
            }
        }
    }

    test_uint(ecs_get(w, inst, DrawInstance)->asset, ecs_lookup(w, "box.editor.gizmos.MoveGizmo"));
    test_assert(!ecs_has_id(w, inst, Hidden));
    test_flt(ecs_get(w, inst, Position)->x, 0);
    test_assert(ecs_lookup_child(w, inst, "x") != 0);
    test_assert(ecs_lookup_child(w, inst, "xy") != 0);
    test_assert(ecs_lookup_child(w, inst, "xyz") != 0);
    test_assert(ecs_has_id(w, ecs_lookup_child(w, inst, "x"), Pickable));

    box_editor_get(w)->tool = ToolRotate;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    it = ecs_each(w, DrawInstance);
    int32_t shown = 0;
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const DrawInstance *d = ecs_get(w, it.entities[i], DrawInstance);
            if (!ecs_has_id(w, it.entities[i], Hidden) && d->asset != edge) {
                test_uint(d->asset, ecs_lookup(w, "box.editor.gizmos.RotateGizmo"));
                shown ++;
            }
        }
    }

    test_int(shown, 1);
    box_editor_select(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, inst, Hidden));
}

void Gizmo_gizmo_is_drawn_in_the_overlay_layer(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t layer = box_draw_top_layer(w);
    test_assert(ecs_has_pair(w, inst, Layer, layer));
    ecs_entity_t x = ecs_lookup_child(w, inst, "x");
    test_assert(ecs_has_pair(w, x, Layer, layer));
    test_assert(ecs_has_pair(w, ecs_lookup_child(w, x, "shaft"), Layer, layer));
    test_assert(ecs_lookup(w, "box.draw.top_view") != 0);
}

void Gizmo_gizmo_scales_with_the_distance_to_the_camera(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    test_flt(ecs_get(w, inst, Scale)->x, 10 * BOX_EDITOR_GIZMO_SIZE);
    ecs_set(w, ecs_lookup(w, "cam"), Position, {0, 0, -20});
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_flt(ecs_get(w, inst, Scale)->x, 20 * BOX_EDITOR_GIZMO_SIZE);
}

void Gizmo_handles_are_pickable_through_the_overlay_view(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t x = ecs_lookup_child(w, inst, "x");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx, cy;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.01f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.01f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == x;
        }
    }

    test_bool(found, true);
    test_uint(hit.view, view);
    test_assert(box_picking_pick(w, 0, (vec2){0, 0}, &hit));
    test_uint(hit.entity, rock);
}

void Gizmo_dragging_the_x_arrow_moves_along_x(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t handle = ecs_lookup_child(w, inst, "x");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.01f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.01f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == handle;
        }
    }

    test_bool(found, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, true);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {wx + i * 20, wy + i * 20} });
        ecs_progress(w, 0);
    }

    const Position *p = ecs_get(w, rock, Position);
    test_assert(p->x > 0.5f);
    test_flt(p->y, 0);
    test_flt(p->z, 0);
}

void Gizmo_dragging_the_xy_plane_keeps_z(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t handle = ecs_lookup_child(w, inst, "xy");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.005f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.005f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == handle;
        }
    }

    test_bool(found, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {wx + i * 20, wy - i * 20} });
        ecs_progress(w, 0);
    }

    const Position *p = ecs_get(w, rock, Position);
    test_assert(p->x > 0.3f);
    test_assert(p->y > 0.3f);
    test_flt(p->z, 0);
}

void Gizmo_dragging_the_center_moves_in_the_view_plane(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 360} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    test_assert(ecs_has_id(w, ecs_lookup_child(w, inst, "xyz"), Hovered));
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {640 + i * 20, 360 - i * 10} });
        ecs_progress(w, 0);
    }

    const Position *p = ecs_get(w, rock, Position);
    test_assert(p->x > 0.3f);
    test_assert(p->y > 0.1f);
    test_flt(p->z, 0);
}

void Gizmo_rotating_about_y_changes_y_only(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolRotate;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t handle = ecs_lookup_child(w, inst, "y");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.005f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.005f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == handle;
        }
    }

    test_bool(found, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){
            .kind = InputMouseMove, .value = {wx + i * 20, wy - i * 20} });
        ecs_progress(w, 0);
    }

    const Rotation *r = ecs_get(w, rock, Rotation);
    test_assert(r != NULL);
    test_assert(fabsf(r->y) > 0.05f);
    test_assert(fabsf(r->x) < 0.001f);
    test_assert(fabsf(r->z) < 0.001f);
}

void Gizmo_scaling_along_x_changes_x_only(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolScale;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t handle = ecs_lookup_child(w, inst, "x");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.005f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.005f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == handle;
        }
    }

    test_bool(found, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx + i * 20, wy} });
        ecs_progress(w, 0);
    }

    const Scale *s = ecs_get(w, rock, Scale);
    test_assert(s != NULL);
    test_assert(s->x > 1.05f);
    test_flt(s->y, 1);
    test_flt(s->z, 1);
}

void Gizmo_release_drops_the_grab(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 360} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 2 });
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, true);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, false);
}

void Gizmo_tool_change_drops_the_grab(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {640, 360} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 20 });
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, true);
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.editor\nexport const t = editor.tool(ToolScale)\n" });
    test_assert(s != 0);
    test_bool(box_editor_state(w)->grab.active, false);
    test_int((int)ecs_singleton_get(w, Editor)->tool, (int)ToolScale);
}

void Gizmo_gizmo_of_a_child_moves_it_in_its_parent(void)
{
    // editor/gizmos
    ecs_entity_t parent = ecs_entity(w, { .name = "parent" });
    ecs_set(w, parent, Position, {3, 0, 0});
    ecs_entity_t child = ecs_entity(w, { .name = "child", .parent = parent });
    ecs_set(w, child, Position, {0, 0, 0});
    ecs_set(w, child, Sphere, { .radius = 0.5f });
    ecs_progress(w, 0);
    box_editor_select(w, child);
    box_editor_get(w)->tool = ToolMove;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    test_flt(ecs_get(w, inst, Position)->x, 3);
    ecs_entity_t handle = ecs_lookup_child(w, inst, "x");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.01f) {
        for (cx = -0.2f; cx <= 0.9f && !found; cx += 0.01f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == handle;
        }
    }

    test_bool(found, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 8 });
    ecs_progress(w, 0);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx + i * 20, wy} });
        ecs_progress(w, 0);
    }

    const Position *p = ecs_get(w, child, Position);
    test_assert(p->x > 0.3f);
    test_flt(p->y, 0);
    test_flt(ecs_get(w, parent, Position)->x, 3);
}

void Gizmo_dragged_handle_stays_lit_over_another_handle(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    box_editor_get(w)->tool = ToolRotate;
    box_editor_modified(w);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t inst = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        inst = it.entities[0];
    }

    ecs_entity_t y = ecs_lookup_child(w, inst, "y");
    ecs_entity_t x = ecs_lookup_child(w, inst, "x");
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit, hit_x;
    bool found = false, found_x = false;
    float cx = 0, cy = 0;
    for (cy = -0.5f; cy <= 0.5f && !found; cy += 0.005f) {
        for (cx = -0.5f; cx <= 0.5f && !found; cx += 0.005f) {
            found = box_picking_pick(w, view, (vec2){cx, cy}, &hit) && hit.entity == y;
        }
    }

    for (cy = -0.5f; cy <= 0.5f && !found_x; cy += 0.005f) {
        for (cx = -0.5f; cx <= 0.5f && !found_x; cx += 0.005f) {
            found_x = box_picking_pick(w, view, (vec2){cx, cy}, &hit_x) && hit_x.entity == x;
        }
    }

    test_bool(found, true);
    test_bool(found_x, true);
    float wx = (hit.cursor.x + 1) * 0.5f * 1280, wy = (1 - hit.cursor.y) * 0.5f * 720;
    float xx = (hit_x.cursor.x + 1) * 0.5f * 1280, xy = (1 - hit_x.cursor.y) * 0.5f * 720;
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_int(ecs_get(w, y, Tint)->a, 255);
    box_input_inject(w, &(InputEvent){
        .kind = InputButtonDown, .code = BoxKeyMouseLeft, .duration = 20 });
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, true);
    test_uint(ecs_singleton_get(w, Editor)->handle, y);
    box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {xx, xy} });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_bool(box_editor_state(w)->grab.active, true);
    test_assert(ecs_has_id(w, x, Hovered));
    test_int(ecs_get(w, y, Tint)->a, 255);
    test_int(ecs_get(w, x, Tint)->a, 0);
    int32_t i;
    for (i = 0; i < 25; i ++) {
        ecs_progress(w, 0);
    }

    test_bool(box_editor_state(w)->grab.active, false);
    test_uint(ecs_singleton_get(w, Editor)->handle, 0);
    test_int(ecs_get(w, y, Tint)->a, 0);
    test_int(ecs_get(w, x, Tint)->a, 255);
}

void Gizmo_backspace_deletes_the_selection(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyBackspace });
    ecs_progress(w, 0);
    test_assert(!ecs_is_alive(w, rock));
    ecs_progress(w, 0);
    test_uint(ecs_singleton_get(w, Editor)->selected, 0);
    test_assert(ecs_lookup(w, "scene") != 0);
}

void Gizmo_backspace_in_a_text_input_keeps_the_selection(void)
{
    // editor/gizmos
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_entity_t field = ecs_new(w);
    ecs_set(w, field, TextInput, { .focused = true });
    box_input_inject(w, &(InputEvent){ .kind = InputKeyDown, .code = BoxKeyBackspace });
    ecs_progress(w, 0);
    test_assert(ecs_is_alive(w, rock));
    test_uint(ecs_singleton_get(w, Editor)->selected, rock);
}
