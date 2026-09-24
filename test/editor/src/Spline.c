/**
 * @file Spline.c
 * @brief Tests for the spline editor: the handles, the guide and the point operations.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/editor/editor_internal.h"

static ecs_world_t *w;
static ecs_entity_t road;
static ecs_entity_t rock;

void Spline_setup(void)
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
    road = ecs_entity(w, { .name = "road", .parent = scene });
    ecs_set(w, road, Position, {0, -2, 0});
    Spline *spline = ecs_ensure(w, road, Spline);
    ecs_vec_init_t(NULL, &spline->points, vec3, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(-2, 0, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(0, 0, 0);
    *ecs_vec_append_t(NULL, &spline->points, vec3) = vec3_new(2, 0, 0);
    spline->tension = 0.5f;
    ecs_modified(w, road, Spline);
    rock = ecs_entity(w, { .name = "rock", .parent = scene });
    ecs_set(w, rock, Position, {0, 3, 0});
    ecs_set(w, rock, Sphere, { .radius = 1 });
    box_editor_enable(w, true);
    ecs_progress(w, 0);
}

void Spline_teardown(void)
{
    ecs_fini(w);
}

void Spline_selecting_a_spline_draws_a_handle_per_point(void)
{
    // editor/spline
    box_editor_select(w, road);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t asset = ecs_lookup(w, "box.editor.gizmos.PointHandle");
    test_assert(asset != 0);
    int32_t handles = 0;
    bool index[3] = { false, false, false };
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const DrawInstance *d = ecs_get(w, it.entities[i], DrawInstance);
            if (d->asset == asset && !ecs_has_id(w, it.entities[i], Hidden)) {
                const GizmoHandle *h = ecs_get(w, it.entities[i], GizmoHandle);
                test_assert(h != NULL);
                test_int((int)h->mode, (int)HandlePoint);
                index[h->index] = true;
                handles ++;
            }
        }
    }

    test_int(handles, 3);
    test_bool(index[0] && index[1] && index[2], true);
}

void Spline_handles_sit_on_the_world_points(void)
{
    // editor/spline
    box_editor_select(w, road);
    ecs_progress(w, 0);
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const GizmoHandle *h = ecs_get(w, it.entities[i], GizmoHandle);
            const Position *p = ecs_get(w, it.entities[i], Position);
            test_assert(h != NULL);
            test_flt(p->x, -2 + 2 * h->index);
            test_flt(p->y, -2);
            test_flt(p->z, 0);
        }
    }
}

void Spline_guide_follows_the_selected_spline(void)
{
    // editor/spline
    box_editor_select(w, road);
    ecs_progress(w, 0);
    ecs_entity_t guide = ecs_lookup(w, "box.editor.guide");
    test_assert(guide != 0);
    test_assert(!ecs_has_id(w, guide, Hidden));
    const SplineMesh *mesh = ecs_get(w, guide, SplineMesh);
    test_assert(mesh != NULL);
    test_uint(mesh->spline, road);
    test_flt(ecs_get(w, guide, Position)->y, -2);
    test_assert(ecs_has_pair(w, guide, Layer, box_draw_top_layer(w)));
}

void Spline_guide_is_hidden_without_a_spline(void)
{
    // editor/spline
    box_editor_select(w, road);
    ecs_progress(w, 0);
    box_editor_select(w, rock);
    ecs_progress(w, 0);
    ecs_entity_t guide = ecs_lookup(w, "box.editor.guide");
    test_assert(guide != 0);
    test_assert(ecs_has_id(w, guide, Hidden));
    ecs_entity_t asset = ecs_lookup(w, "box.editor.gizmos.PointHandle");
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_get(w, it.entities[i], DrawInstance)->asset == asset) {
                test_assert(ecs_has_id(w, it.entities[i], Hidden));
            }
        }
    }
}

void Spline_point_selects_a_control_point(void)
{
    // editor/spline
    test_bool(box_editor_point(w, 1), false);
    box_editor_select(w, road);
    test_bool(box_editor_point(w, 1), true);
    test_int(ecs_singleton_get(w, Editor)->point, 1);
    test_bool(box_editor_point(w, 5), false);
    test_int(ecs_singleton_get(w, Editor)->point, 1);
    test_bool(box_editor_point(w, -1), true);
    test_int(ecs_singleton_get(w, Editor)->point, -1);
}

void Spline_point_add_inserts_after_the_selected_point(void)
{
    // editor/spline
    box_editor_select(w, road);
    box_editor_point(w, 0);
    test_bool(box_editor_point_add(w), true);
    const Spline *spline = ecs_get(w, road, Spline);
    test_int(ecs_vec_count(&spline->points), 4);
    test_flt(ecs_vec_get_t(&spline->points, vec3, 1)->x, -1);
    test_flt(ecs_vec_get_t(&spline->points, vec3, 2)->x, 0);
    test_int(ecs_singleton_get(w, Editor)->point, 1);
}

void Spline_point_add_extends_the_end(void)
{
    // editor/spline
    box_editor_select(w, road);
    test_bool(box_editor_point_add(w), true);
    const Spline *spline = ecs_get(w, road, Spline);
    test_int(ecs_vec_count(&spline->points), 4);
    test_flt(ecs_vec_get_t(&spline->points, vec3, 3)->x, 4);
    test_int(ecs_singleton_get(w, Editor)->point, 3);
    box_editor_select(w, rock);
    test_bool(box_editor_point_add(w), false);
}

void Spline_point_remove_deletes_the_selected_point(void)
{
    // editor/spline
    box_editor_select(w, road);
    test_bool(box_editor_point_remove(w), false);
    box_editor_point(w, 1);
    test_bool(box_editor_point_remove(w), true);
    const Spline *spline = ecs_get(w, road, Spline);
    test_int(ecs_vec_count(&spline->points), 2);
    test_flt(ecs_vec_get_t(&spline->points, vec3, 1)->x, 2);
    test_int(ecs_singleton_get(w, Editor)->point, 1);
    test_bool(box_editor_point_remove(w), true);
    test_int(ecs_singleton_get(w, Editor)->point, 0);
}

void Spline_dragging_a_handle_moves_its_point(void)
{
    // editor/spline
    box_editor_select(w, road);
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    ecs_entity_t handle = 0;
    ecs_iter_t it = ecs_each(w, DrawInstance);
    while (ecs_each_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (ecs_get(w, it.entities[i], GizmoHandle)->index == 2) {
                handle = it.entities[i];
            }
        }
    }

    test_assert(handle != 0);
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    PickHit hit;
    bool found = false;
    float cx = 0, cy = 0;
    for (cy = -0.6f; cy <= 0.2f && !found; cy += 0.005f) {
        for (cx = 0; cx <= 0.8f && !found; cx += 0.005f) {
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
    ecs_progress(w, 0);
    test_int(ecs_singleton_get(w, Editor)->point, 2);
    int32_t i;
    for (i = 1; i <= 5; i ++) {
        box_input_inject(w, &(InputEvent){ .kind = InputMouseMove, .value = {wx, wy - i * 20} });
        ecs_progress(w, 0);
    }

    const Spline *spline = ecs_get(w, road, Spline);
    test_assert(ecs_vec_get_t(&spline->points, vec3, 2)->y > 0.3f);
    test_flt(ecs_vec_get_t(&spline->points, vec3, 0)->y, 0);
}
