/**
 * @file Layers.c
 * @brief Tests for render layers: membership masks, view lists and grouped slots.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Layers_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    packet.clear[3] = 1.0f;
}

void Layers_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Layers_membership_assigns_a_bit(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, Layer, a);
    box_render_extract(w, &packet);

    RenderState *state = box_render_state(w);
    test_bool(state->layer_used, true);
    test_int(ecs_vec_count(&state->view_layers), 1);
    test_uint(ecs_vec_get_t(&state->view_layers, ecs_entity_t, 0)[0], a);
    test_uint(ecs_get(w, e, RenderInstance)->layer_mask, 1u);
}

void Layers_entity_in_two_layers_has_both_bits(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, Layer, a);
    ecs_add_pair(w, e, Layer, b);
    box_render_extract(w, &packet);

    RenderState *state = box_render_state(w);
    test_int(ecs_vec_count(&state->view_layers), 2);
    test_uint(ecs_get(w, e, RenderInstance)->layer_mask, 3u);
}

void Layers_entity_without_a_layer_has_no_bits(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {0, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Position, {2, 0, 0});
    ecs_set(w, other, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    test_uint(ecs_get(w, other, RenderInstance)->layer_mask, 0);
    test_bool(box_render_layer_visible(0, 1u), true);
    test_bool(box_render_layer_visible(2u, 1u), false);
}

void Layers_empty_view_list_shows_everything(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, Layer, a);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, BOX_VIEW_LAYER_ALL);
    test_uint(v->caster_mask, BOX_VIEW_LAYER_ALL);
}

void Layers_view_list_restricts_the_mask(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {0, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Position, {2, 0, 0});
    ecs_set(w, other, Box, {1, 1, 1});
    ecs_add_pair(w, other, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, 1u);
    test_bool(box_render_layer_visible(
        ecs_get(w, member, RenderInstance)->layer_mask, v->layer_mask), true);
    test_bool(box_render_layer_visible(
        ecs_get(w, other, RenderInstance)->layer_mask, v->layer_mask), false);
}

void Layers_static_slots_are_grouped_by_mask(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {0, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {2, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t tail = ecs_new(w);
    ecs_set(w, tail, Position, {4, 0, 0});
    ecs_set(w, tail, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    test_int(packet.static_groups.count, 2);
    box_static_group_t *first = box_vec_at(&packet.static_groups, 0);
    box_static_group_t *second = box_vec_at(&packet.static_groups, 1);
    test_uint(first->mask, 0);
    test_int(first->first, 0);
    test_int(first->count, 2);
    test_uint(second->mask, 1u);
    test_int(second->first, 2);
    test_int(second->count, 1);
    test_uint(ecs_get(w, member, RenderInstance)->slot, 2);
    test_uint(ecs_get(w, plain, RenderInstance)->layer_mask, 0);
    test_uint(ecs_get(w, tail, RenderInstance)->layer_mask, 0);
}

void Layers_leaving_a_layer_regroups_the_slots(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {0, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {2, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    box_render_extract(w, &packet);
    test_int(packet.static_groups.count, 2);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_remove_pair(w, member, Layer, a);
    box_render_extract(w, &packet);

    test_int(packet.static_groups.count, 1);
    box_static_group_t *only = box_vec_at(&packet.static_groups, 0);
    test_uint(only->mask, 0);
    test_int(only->first, 0);
    test_int(only->count, 2);
    test_uint(ecs_get(w, member, RenderInstance)->layer_mask, 0);
    test_uint(ecs_get(w, plain, RenderInstance)->layer_mask, 0);
}

void Layers_dynamic_instances_are_filtered_per_view(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 0, 0});
    ecs_set(w, shown, Box, {1, 1, 1});
    ecs_add_id(w, shown, DynamicTransform);
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t hidden = ecs_new(w);
    ecs_set(w, hidden, Position, {2, 0, 0});
    ecs_set(w, hidden, Box, {1, 1, 1});
    ecs_add_id(w, hidden, DynamicTransform);
    ecs_add_pair(w, hidden, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(packet.dynamic_instances.count, 1);
    test_int(v->dynamic_first, 0);
    test_int(v->dynamic_count, 1);
    box_dynamic_batch_t *batch = box_vec_at(&packet.dynamic_batches, 0);
    test_int(batch->count, 1);
}

void Layers_transparent_entries_are_filtered_per_view(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 0, 0});
    ecs_set(w, shown, Box, {1, 1, 1});
    ecs_set(w, shown, Opacity, {0.5f});
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t hidden = ecs_new(w);
    ecs_set(w, hidden, Position, {2, 0, 0});
    ecs_set(w, hidden, Box, {1, 1, 1});
    ecs_set(w, hidden, Opacity, {0.5f});
    ecs_add_pair(w, hidden, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->transparent_count, 1);
    test_int(packet.transparent.count, 1);
}

void Layers_lights_are_restricted_to_a_layer(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 1, 0});
    ecs_set(w, shown, PointLight, {.intensity = 1, .range = 5});
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t hidden = ecs_new(w);
    ecs_set(w, hidden, Position, {1, 1, 0});
    ecs_set(w, hidden, PointLight, {.intensity = 1, .range = 5});
    ecs_add_pair(w, hidden, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->light_count, 1);
    box_light_t *light = box_vec_at(&packet.lights, v->light_first);
    test_flt(light->position[0], 0);
}

void Layers_two_views_show_their_own_layer(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t red = ecs_new(w);
    ecs_set(w, red, Position, {0, 0, 0});
    ecs_set(w, red, Box, {1, 1, 1});
    ecs_add_pair(w, red, Layer, a);
    ecs_entity_t blue = ecs_new(w);
    ecs_set(w, blue, Position, {2, 0, 0});
    ecs_set(w, blue, Box, {1, 1, 1});
    ecs_add_pair(w, blue, Layer, b);
    ecs_entity_t both = ecs_new(w);
    ecs_set(w, both, Position, {4, 0, 0});
    ecs_set(w, both, Box, {1, 1, 1});
    ecs_add_pair(w, both, Layer, a);
    ecs_add_pair(w, both, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});

    ecs_entity_t first = ecs_new(w);
    ecs_add(w, first, View);
    View *first_settings = ecs_get_mut(w, first, View);
    first_settings->camera = cam;
    first_settings->layers[0] = a;
    ecs_modified(w, first, View);
    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, View);
    View *second_settings = ecs_get_mut(w, second, View);
    second_settings->camera = cam;
    second_settings->priority = 1;
    second_settings->layers[0] = b;
    ecs_modified(w, second, View);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    uint32_t red_mask = ecs_get(w, red, RenderInstance)->layer_mask;
    uint32_t blue_mask = ecs_get(w, blue, RenderInstance)->layer_mask;
    uint32_t both_mask = ecs_get(w, both, RenderInstance)->layer_mask;
    box_view_t *view_a = box_vec_at(&packet.views, 0);
    box_view_t *view_b = box_vec_at(&packet.views, 1);
    test_bool(box_render_layer_visible(red_mask, view_a->layer_mask), true);
    test_bool(box_render_layer_visible(blue_mask, view_a->layer_mask), false);
    test_bool(box_render_layer_visible(both_mask, view_a->layer_mask), true);
    test_bool(box_render_layer_visible(red_mask, view_b->layer_mask), false);
    test_bool(box_render_layer_visible(blue_mask, view_b->layer_mask), true);
    test_bool(box_render_layer_visible(both_mask, view_b->layer_mask), true);
}

void Layers_view_list_holds_eight_entries(void)
{
    // views/layers
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    test_int(sizeof(settings->layers) / sizeof(settings->layers[0]), BOX_VIEW_LAYER_MAX);

    int32_t i;
    for (i = 0; i < BOX_VIEW_LAYER_MAX; i ++) {
        ecs_entity_t layer = ecs_new(w);
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)i, 0, 0});
        ecs_set(w, e, Box, {1, 1, 1});
        ecs_add_pair(w, e, Layer, layer);
        settings->layers[i] = layer;
    }

    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, 0xFFu);

    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_get_mut(w, view, View)->layers[3] = 0;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);
    test_uint(((box_view_t*)box_vec_at(&packet.views, 0))->layer_mask, 0x7u);
}

void Layers_past_the_bit_limit_get_no_bit(void)
{
    // views/layers
    ecs_entity_t last = 0;
    int32_t i;
    for (i = 0; i <= BOX_VIEW_LAYER_BITS; i ++) {
        ecs_entity_t layer = ecs_new(w);
        last = ecs_new(w);
        ecs_set(w, last, Position, {(float)i, 0, 0});
        ecs_set(w, last, Box, {1, 1, 1});
        ecs_add_pair(w, last, Layer, layer);
    }

    box_render_extract(w, &packet);

    RenderState *state = box_render_state(w);
    test_int(ecs_vec_count(&state->view_layers), BOX_VIEW_LAYER_BITS);
    test_bool(state->layer_full, true);
    test_uint(ecs_get(w, last, RenderInstance)->layer_mask, 0);
}

void Layers_prefab_layer_is_inherited(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_add_pair(w, prefab, Layer, a);
    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    test_uint(ecs_get(w, e, RenderInstance)->layer_mask, 1u);
    test_int(packet.static_groups.count, 1);
    test_uint(((box_static_group_t*)box_vec_at(&packet.static_groups, 0))->mask, 1u);
}

void Layers_caster_mask_comes_from_shadow_layers(void)
{
    // views/layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t b = ecs_new(w);
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, Layer, a);
    ecs_entity_t other = ecs_new(w);
    ecs_set(w, other, Position, {2, 0, 0});
    ecs_set(w, other, Box, {1, 1, 1});
    ecs_add_pair(w, other, Layer, b);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->shadow_layers[0] = b;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, BOX_VIEW_LAYER_ALL);
    test_uint(v->caster_mask, 2u);
}

void Layers_scene_without_layers_keeps_its_packet(void)
{
    // views/layers
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t light = ecs_new(w);
    ecs_set(w, light, Position, {0, 2, 0});
    ecs_set(w, light, PointLight, {.intensity = 1, .range = 8});
    int32_t i;
    for (i = 0; i < 8; i ++) {
        ecs_entity_t e = ecs_new(w);
        ecs_set(w, e, Position, {(float)i, 0, 0});
        ecs_set(w, e, Box, {1, 1, 1});
        if (i & 1) {
            ecs_add_id(w, e, DynamicTransform);
        }
    }

    box_render_extract(w, &packet);

    RenderState *state = box_render_state(w);
    test_bool(state->layer_used, false);
    test_int(ecs_vec_count(&state->view_layers), 0);
    test_int(packet.static_groups.count, 0);
    test_int(packet.static_updates.count, 4);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)box_vec_at(&packet.static_counts, 0))->count, 4);
    test_int(packet.dynamic_instances.count, 4);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, BOX_VIEW_LAYER_ALL);
    test_uint(v->caster_mask, BOX_VIEW_LAYER_ALL);
    test_int(v->dynamic_first, 0);
    test_int(v->dynamic_count, packet.dynamic_batches.count);
    test_int(v->light_count, 1);

    box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, 0);
    test_int(ecs_vec_count(&batch->groups), 0);
}

void Layers_strict_view_defaults_to_off(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {0, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {2, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    test_bool(ecs_get(w, view, View)->strict_layers, false);
    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->strict_layers, 0);
    test_bool(box_view_shows(v, 0), true);
    test_bool(box_view_shows(v, 1u), true);
    test_bool(box_view_shows(v, 2u), false);
}

void Layers_strict_view_hides_unlayered_static_runs(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {0, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {2, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    settings->strict_layers = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->strict_layers, 1);
    test_int(packet.static_groups.count, 2);
    box_static_group_t *first = box_vec_at(&packet.static_groups, 0);
    box_static_group_t *second = box_vec_at(&packet.static_groups, 1);
    test_uint(first->mask, 0);
    test_bool(box_view_shows(v, first->mask), false);
    test_uint(second->mask, 1u);
    test_bool(box_view_shows(v, second->mask), true);
}

void Layers_strict_view_hides_unlayered_dynamic(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 0, 0});
    ecs_set(w, shown, Box, {1, 1, 1});
    ecs_add_id(w, shown, DynamicTransform);
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {2, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_add_id(w, plain, DynamicTransform);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    settings->strict_layers = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(packet.dynamic_instances.count, 1);
    test_int(v->dynamic_count, 1);
    box_instance_t *inst = box_vec_at(&packet.dynamic_instances, 0);
    test_flt(inst->transform[3], 0);
}

void Layers_strict_view_hides_unlayered_transparent(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 0, 0});
    ecs_set(w, shown, Box, {1, 1, 1});
    ecs_set(w, shown, Opacity, {0.5f});
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {2, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_set(w, plain, Opacity, {0.5f});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    settings->strict_layers = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->transparent_count, 1);
    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_flt(entry->data.transform[3], 0);
}

void Layers_strict_view_hides_unlayered_lights(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t shown = ecs_new(w);
    ecs_set(w, shown, Position, {0, 1, 0});
    ecs_set(w, shown, PointLight, {.intensity = 1, .range = 5});
    ecs_add_pair(w, shown, Layer, a);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {1, 1, 0});
    ecs_set(w, plain, PointLight, {.intensity = 1, .range = 5});
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->layers[0] = a;
    settings->strict_layers = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_int(v->light_count, 1);
    box_light_t *light = box_vec_at(&packet.lights, v->light_first);
    test_flt(light->position[0], 0);
}

void Layers_strict_view_without_layers_shows_everything(void)
{
    // views/strict_layers
    ecs_entity_t a = ecs_new(w);
    ecs_entity_t member = ecs_new(w);
    ecs_set(w, member, Position, {0, 0, 0});
    ecs_set(w, member, Box, {1, 1, 1});
    ecs_add_pair(w, member, Layer, a);
    ecs_entity_t plain = ecs_new(w);
    ecs_set(w, plain, Position, {2, 0, 0});
    ecs_set(w, plain, Box, {1, 1, 1});
    ecs_add_id(w, plain, DynamicTransform);
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    View *settings = ecs_get_mut(w, view, View);
    settings->camera = cam;
    settings->strict_layers = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    box_view_t *v = box_vec_at(&packet.views, 0);
    test_uint(v->layer_mask, BOX_VIEW_LAYER_ALL);
    test_uint(v->strict_layers, 0);
    test_bool(box_view_shows(v, 0), true);
    test_int(packet.dynamic_instances.count, 1);
}
