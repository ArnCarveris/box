/**
 * @file Draw.c
 * @brief Tests for box_draw: the pool of instances and the overlay it draws over the scene.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/draw/draw_internal.h"
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static ecs_entity_t prefab;

void Draw_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    ecs_entity_t cam = ecs_entity(w, { .name = "cam" });
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
    prefab = ecs_entity(w, { .name = "Ball" });
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Sphere, { .radius = 1 });
    ecs_set(w, prefab, Rgb, {255, 0, 0});
}

void Draw_teardown(void)
{
    ecs_fini(w);
}

void Draw_prefab_draws_an_instance_under_the_pool(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    test_assert(e != 0);
    test_assert(ecs_has_pair(w, e, EcsIsA, prefab));
    test_assert(ecs_has_id(w, e, DynamicTransform));
    test_uint(ecs_get_parent(w, e), ecs_lookup(w, "box.draw.instances"));
    const DrawInstance *inst = ecs_get(w, e, DrawInstance);
    test_assert(inst != NULL);
    test_uint(inst->asset, prefab);
    test_uint(inst->key, 1);
    test_bool(inst->overlay, false);
    ecs_progress(w, 0);
    test_assert(ecs_get(w, e, WorldTransform) != NULL);
}

void Draw_template_draws_an_instance_with_its_body(void)
{
    // editor/draw
    ecs_entity_t s = ecs_script(w, { .code =
        "using box.geometry\n"
        "using box.material\n"
        "using box.transform\n"
        "template Arrow {\n"
        "  prop length: f32 = 2\n"
        "  shaft {\n"
        "    Cylinder: {length: length, radius: 0.1}\n"
        "    Rgb: {0, 255, 0}\n"
        "  }\n"
        "  tip {\n"
        "    Position: {0, length, 0}\n"
        "    Cone: {length: 0.5, radius: 0.2}\n"
        "  }\n"
        "}\n" });
    test_assert(s != 0);
    ecs_entity_t arrow = ecs_lookup(w, "Arrow");
    test_assert(arrow != 0);
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = arrow, .key = 7 });
    test_assert(e != 0);
    test_assert(ecs_has_id(w, e, arrow));
    ecs_entity_t shaft = ecs_lookup_child(w, e, "shaft");
    ecs_entity_t tip = ecs_lookup_child(w, e, "tip");
    test_assert(shaft != 0);
    test_assert(tip != 0);
    test_assert(ecs_get(w, shaft, Cylinder) != NULL);
    test_flt(ecs_get(w, tip, Position)->y, 2);
}

void Draw_same_key_draws_the_same_instance(void)
{
    // editor/draw
    ecs_entity_t a = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 3 });
    ecs_progress(w, 0);
    ecs_entity_t b = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 3 });
    ecs_entity_t c = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 4 });
    test_uint(a, b);
    test_assert(c != a);
}

void Draw_calls_without_a_key_are_numbered_by_order(void)
{
    // editor/draw
    ecs_entity_t a = box_draw(w, &(box_draw_desc_t){ .asset = prefab });
    ecs_entity_t b = box_draw(w, &(box_draw_desc_t){ .asset = prefab });
    test_assert(a != b);
    test_uint(ecs_get(w, a, DrawInstance)->key, 1);
    test_uint(ecs_get(w, b, DrawInstance)->key, 2);
    ecs_progress(w, 0);
    test_uint(box_draw(w, &(box_draw_desc_t){ .asset = prefab }), a);
    test_uint(box_draw(w, &(box_draw_desc_t){ .asset = prefab }), b);
}

void Draw_instance_not_drawn_again_is_hidden(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    ecs_progress(w, 0);
    test_assert(!ecs_has_id(w, e, Hidden));
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, e, Hidden));
    test_assert(ecs_is_alive(w, e));
}

void Draw_instance_drawn_again_is_shown(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    test_assert(ecs_has_id(w, e, Hidden));
    test_uint(box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 }), e);
    test_assert(!ecs_has_id(w, e, Hidden));
    ecs_progress(w, 0);
    test_assert(!ecs_has_id(w, e, Hidden));
}

void Draw_instance_hidden_for_long_is_deleted(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    int32_t i;
    for (i = 0; i < BOX_DRAW_RETIRE_FRAMES + 1; i ++) {
        ecs_progress(w, 0);
    }

    test_assert(ecs_is_alive(w, e));
    ecs_progress(w, 0);
    test_assert(!ecs_is_alive(w, e));
    ecs_entity_t again = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    test_assert(again != 0);
    test_assert(ecs_is_alive(w, again));
}

void Draw_instance_follows_the_transform_of_the_call(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1,
        .position = {1, 2, 3}, .rotation = {0, 1, 0}, .scale = {2, 2, 2} });
    test_flt(ecs_get(w, e, Position)->z, 3);
    test_flt(ecs_get(w, e, Rotation)->y, 1);
    test_flt(ecs_get(w, e, Scale)->x, 2);
    ecs_progress(w, 0);
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1, .position = {4, 5, 6} });
    ecs_progress(w, 0);
    test_flt(ecs_get(w, e, Position)->x, 4);
    vec3 world_pos = mat4_get_translation(&ecs_get(w, e, WorldTransform)->m);
    test_flt(world_pos.y, 5);
}

void Draw_zero_scale_draws_at_one(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1 });
    test_flt(ecs_get(w, e, Scale)->x, 1);
    test_flt(ecs_get(w, e, Scale)->y, 1);
    test_flt(ecs_get(w, e, Scale)->z, 1);
}

void Draw_live_entity_is_not_drawn(void)
{
    // editor/draw
    ecs_entity_t live = ecs_new(w);
    ecs_set(w, live, Sphere, { .radius = 1 });
    test_uint(box_draw(w, &(box_draw_desc_t){ .asset = live, .key = 1 }), 0);
}

void Draw_overlay_instance_joins_the_overlay_layer(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){
        .asset = prefab, .key = 1, .overlay = true });
    ecs_entity_t layer = box_draw_overlay_layer(w);
    test_assert(layer != 0);
    test_assert(ecs_has_pair(w, e, Layer, layer));
    test_bool(ecs_get(w, e, DrawInstance)->overlay, true);
    ecs_entity_t plain = box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 2 });
    test_assert(!ecs_has_pair(w, plain, Layer, layer));
}

void Draw_overlay_view_draws_the_layer_with_the_scene_camera(void)
{
    // editor/draw
    test_assert(ecs_lookup(w, "box.draw.overlay_view") == 0);
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1, .overlay = true });
    ecs_entity_t view = ecs_lookup(w, "box.draw.overlay_view");
    test_assert(view != 0);
    const View *v = ecs_get(w, view, View);
    test_assert(v != NULL);
    test_uint(v->layers[0], box_draw_overlay_layer(w));
    test_bool(v->strict_layers, true);
    test_int(v->priority, BOX_DRAW_OVERLAY_PRIORITY);
    ecs_progress(w, 0);
    v = ecs_get(w, view, View);
    test_uint(v->camera, ecs_lookup(w, "cam"));
}

void Draw_overlay_view_is_occluded_by_the_scene_view(void)
{
    // editor/draw
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 360;
    ecs_entity_t cam = ecs_lookup(w, "cam");
    ecs_entity_t scene = ecs_entity(w, { .name = "main" });
    ecs_set(w, scene, View, { .camera = cam, .enabled = true });
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1, .overlay = true });
    ecs_entity_t view = ecs_lookup(w, "box.draw.overlay_view");
    test_assert(view != 0);
    test_uint(ecs_get(w, view, View)->occluder, 0);
    box_render_extract(w, &packet);
    ecs_progress(w, 0);
    const View *v = ecs_get(w, view, View);
    test_uint(v->camera, cam);
    test_uint(v->occluder, scene);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 2);
    test_uint(((box_view_t*)box_vec_at(&packet.views, 1))->occluder, 1);
    ecs_delete(w, scene);
    box_render_extract(w, &packet);
    ecs_progress(w, 0);
    test_uint(ecs_get(w, view, View)->occluder, 0);
    box_packet_fini(&packet);
}

void Draw_overlay_layer_is_hidden_from_the_scene_view(void)
{
    // editor/draw
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1, .overlay = true });
    test_bool(box_render_is_ui_layer(w, box_draw_overlay_layer(w)), true);
}

void Draw_on_top_instance_joins_the_top_layer(void)
{
    // editor/draw
    ecs_entity_t e = box_draw(w, &(box_draw_desc_t){
        .asset = prefab, .key = 1, .overlay = true, .on_top = true });
    ecs_entity_t top = box_draw_top_layer(w);
    test_assert(top != 0);
    test_assert(top != box_draw_overlay_layer(w));
    test_assert(ecs_has_pair(w, e, Layer, top));
    test_assert(!ecs_has_pair(w, e, Layer, box_draw_overlay_layer(w)));
    test_bool(ecs_get(w, e, DrawInstance)->on_top, true);
    test_bool(box_render_is_ui_layer(w, top), true);
    test_assert(ecs_lookup(w, "box.draw.overlay_view") == 0);
    ecs_entity_t view = ecs_lookup(w, "box.draw.top_view");
    test_assert(view != 0);
    const View *v = ecs_get(w, view, View);
    test_uint(v->layers[0], top);
    test_bool(v->strict_layers, true);
    test_int(v->priority, BOX_DRAW_TOP_PRIORITY);
}

void Draw_top_view_is_not_occluded_by_the_scene_view(void)
{
    // editor/draw
    ecs_entity_t cam = ecs_lookup(w, "cam");
    ecs_entity_t scene = ecs_entity(w, { .name = "main" });
    ecs_set(w, scene, View, { .camera = cam, .enabled = true });
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 1, .overlay = true });
    box_draw(w, &(box_draw_desc_t){ .asset = prefab, .key = 2, .overlay = true, .on_top = true });
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 360;
    box_render_extract(w, &packet);
    ecs_progress(w, 0);
    const View *overlay = ecs_get(w, ecs_lookup(w, "box.draw.overlay_view"), View);
    const View *top = ecs_get(w, ecs_lookup(w, "box.draw.top_view"), View);
    test_uint(overlay->camera, cam);
    test_uint(overlay->occluder, scene);
    test_uint(top->camera, cam);
    test_uint(top->occluder, 0);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 3);
    box_packet_fini(&packet);
}
