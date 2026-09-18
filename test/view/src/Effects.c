/**
 * @file Effects.c
 * @brief Tests for the post process chain of a view: order, inputs and targets.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t fx_shader;

void Effects_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    fx_shader = ecs_lookup(w, "box.effects.shaders.color_grade");
}

void Effects_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Effects_builtin_grade_is_off_by_default(void)
{
    // effects/shader_graph
    ecs_entity_t grade = ecs_lookup(w, "box.effects.stack.color_grade");
    test_assert(grade != 0);
    test_assert(fx_shader != 0);
    const Effect *fx = ecs_get(w, grade, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_uint(fx->shader, fx_shader);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
    test_int(packet.view.effect_inputs, 0);
}

void Effects_chain_runs_in_order(void)
{
    // effects/shader_graph
    ecs_entity_t late = ecs_new(w);
    ecs_set(w, late, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .scale = 1 });
    ecs_set(w, late, ColorGrade, { .exposure = 2.0f, .saturation = 1.0f });
    ecs_entity_t early = ecs_new(w);
    ecs_set(w, early, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_set(w, early, ColorGrade, { .exposure = 1.0f, .saturation = 1.0f });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 2);
    test_int(packet.view.effect_first, 0);
    test_int(packet.view.effect_count, 2);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 0))->params[0], 1.0f);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 1))->params[0], 2.0f);
}

void Effects_disabled_effect_is_skipped(void)
{
    // effects/shader_graph
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 1);

    box_packet_reset(&packet);
    ecs_get_mut(w, e, Effect)->enabled = false;
    ecs_modified(w, e, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
}

void Effects_enabling_an_effect_rebuilds_the_chain(void)
{
    // effects/shader_graph
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader, .order = 10, .enabled = false,
        .color = true, .scale = 1 });
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 0);

    box_packet_reset(&packet);
    ecs_get_mut(w, e, Effect)->enabled = true;
    ecs_modified(w, e, Effect);
    box_render_extract(w, &packet);

    test_int(packet.view.effect_count, 1);
}

void Effects_effect_without_a_shader_is_skipped(void)
{
    // effects/shader_graph
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = 0, .order = 10, .enabled = true, .color = true,
        .scale = 1 });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
}

void Effects_view_list_overrides_the_stack(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_set(w, a, ColorGrade, { .exposure = 1.0f, .saturation = 1.0f });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .scale = 1 });
    ecs_set(w, b, ColorGrade, { .exposure = 2.0f, .saturation = 1.0f });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_get_mut(w, view, View)->effect_list[0] = b;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 1);
    test_int(packet.view.effect_count, 1);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 0))->params[0], 2.0f);
}

void Effects_view_without_a_list_runs_the_stack(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .scale = 1 });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    test_int(packet.view.effect_count, 2);
}

void Effects_view_toggle_skips_the_chain(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    ecs_entity_t view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_get_mut(w, view, View)->effects = false;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
    test_int(packet.view.effects, 0);
}

void Effects_inputs_are_the_union_of_the_passes(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .depth = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .normals = true, .motion = true, .scale = 1 });
    box_render_extract(w, &packet);

    test_int(packet.view.effect_inputs,
        BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH | BOX_EFFECT_NORMALS | BOX_EFFECT_MOTION);
    test_int(((box_effect_t*)box_vec_at(&packet.effects, 0))->inputs,
        BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH);
}

void Effects_disabled_pass_does_not_ask_for_its_inputs(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = false,
        .color = true, .normals = true, .scale = 1 });
    box_render_extract(w, &packet);

    test_int(packet.view.effect_count, 1);
    test_int(packet.view.effect_inputs, BOX_EFFECT_COLOR);
}

void Effects_parameters_come_from_the_parameter_component(void)
{
    // effects/shader_graph
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_set(w, e, ColorGrade, { .exposure = 0.75f, .saturation = 1.5f });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->params[0], 0.75f);
    test_flt(pass->params[1], 1.5f);
    test_flt(pass->params[2], 0.0f);
}

void Effects_two_passes_of_one_scale_share_a_target_size(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .scale = 1 });
    box_render_extract(w, &packet);

    test_int(packet.view.effect_count, 2);
    uint32_t aw, ah, bw, bh;
    box_effect_size(packet.view.width, packet.view.height,
        ((box_effect_t*)box_vec_at(&packet.effects, 0))->scale, &aw, &ah);
    box_effect_size(packet.view.width, packet.view.height,
        ((box_effect_t*)box_vec_at(&packet.effects, 1))->scale, &bw, &bh);
    test_uint(aw, bw);
    test_uint(ah, bh);
    test_uint(aw, 640);
    test_uint(ah, 480);
}

void Effects_half_scale_pass_asks_for_half_the_target(void)
{
    // effects/shader_graph
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 0.5f });
    box_render_extract(w, &packet);

    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->scale, 0.5f);
    uint32_t width, height;
    box_effect_size(packet.view.width, packet.view.height, pass->scale, &width, &height);
    test_uint(width, 320);
    test_uint(height, 240);
    box_effect_size(1, 1, 0.5f, &width, &height);
    test_uint(width, 1);
    test_uint(height, 1);
}

void Effects_two_views_keep_their_own_range(void)
{
    // effects/shader_graph
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .order = 10, .enabled = true,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .order = 20, .enabled = true,
        .color = true, .scale = 1 });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    ecs_entity_t first = ecs_new(w);
    ecs_add(w, first, View);
    ecs_get_mut(w, first, View)->camera = cam;
    ecs_modified(w, first, View);
    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, View);
    ecs_get_mut(w, second, View)->camera = cam;
    ecs_get_mut(w, second, View)->priority = 1;
    ecs_get_mut(w, second, View)->effect_list[0] = a;
    ecs_modified(w, second, View);
    box_render_extract(w, &packet);

    test_int(packet.views.count, 2);
    box_view_t *v0 = box_vec_at(&packet.views, 0);
    box_view_t *v1 = box_vec_at(&packet.views, 1);
    test_int(v0->effect_first, 0);
    test_int(v0->effect_count, 2);
    test_int(v1->effect_first, 2);
    test_int(v1->effect_count, 1);
    test_int(packet.effects.count, 3);
}
