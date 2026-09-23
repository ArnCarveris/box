/**
 * @file Effects.c
 * @brief Tests for the per view post processing effect chain.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t fx_shader;
static ecs_entity_t grade_params;
static ecs_entity_t view;

static void add_effect(
    ecs_entity_t view_entity,
    ecs_entity_t effect,
    bool enabled)
{
    View *settings = ecs_get_mut(w, view_entity, View);
    ecs_vec_append_t(NULL, &settings->effects, ViewEffect)[0] =
        (ViewEffect){.effect = effect, .enabled = enabled};
    ecs_modified(w, view_entity, View);
}

static void set_grade(
    ecs_entity_t e,
    float exposure,
    float saturation)
{
    const ecs_type_info_t *ti = ecs_get_type_info(w, grade_params);
    void *ptr = ecs_ensure_id(w, e, grade_params, (size_t)ti->size);
    ecs_value_t value = { .type = grade_params, .ptr = ptr };
    char expr[96];
    snprintf(expr, sizeof(expr), "{exposure: %g, saturation: %g}",
        exposure, saturation);
    test_assert(ecs_expr_run(w, expr, &value, NULL) != NULL);
    ecs_modified_id(w, e, grade_params);
}

void Effects_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    test_assert(box_script_load(w, BOX_EFFECTS_BUILTINS) != 0);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    fx_shader = ecs_lookup(w, "box.effects.shaders.color_grade");
    grade_params = ecs_lookup(w, "box.effects.ColorGrade");
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -5});
    ecs_set(w, cam, Camera, {.fov = 90});
    view = ecs_new(w);
    ecs_add(w, view, View);
    ecs_get_mut(w, view, View)->camera = cam;
    ecs_modified(w, view, View);
}

void Effects_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    for (i = 0; i < packet.effect_texture_create.count; i ++) {
        free(((box_effect_texture_cmd_t*)box_vec_at(
            &packet.effect_texture_create, i))->texels);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Effects_builtin_grade_is_a_prefab(void)
{
    ecs_entity_t grade = ecs_lookup(w, "box.effects.color_grade");
    test_assert(grade != 0);
    test_assert(fx_shader != 0);
    const Effect *fx = ecs_get(w, grade, Effect);
    test_assert(fx != NULL);
    test_bool(ecs_has_id(w, grade, EcsPrefab), true);
    test_uint(fx->shader, fx_shader);
    test_uint(fx->params, grade_params);
    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
}

void Effects_chain_runs_in_view_order(void)
{
    ecs_entity_t late = ecs_new(w);
    ecs_set(w, late, Effect, { .shader = fx_shader, .params = grade_params,
        .color = true, .scale = 1 });
    set_grade(late, 2, 1);
    ecs_entity_t early = ecs_new(w);
    ecs_set(w, early, Effect, { .shader = fx_shader, .params = grade_params,
        .color = true, .scale = 1 });
    set_grade(early, 1, 1);
    add_effect(view, late, true);
    add_effect(view, early, true);
    box_render_extract(w, &packet);
    test_int(packet.effects.count, 2);
    test_int(packet.view.effect_first, 0);
    test_int(packet.view.effect_count, 2);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 0))->params[0], 2);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 1))->params[0], 1);
}

void Effects_disabled_view_entry_is_skipped(void)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    add_effect(view, e, false);
    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
}

void Effects_enabling_a_view_entry_rebuilds_the_chain(void)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    add_effect(view, e, false);
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 0);
    box_packet_reset(&packet);
    View *settings = ecs_get_mut(w, view, View);
    ecs_vec_first_t(&settings->effects, ViewEffect)[0].enabled = true;
    ecs_modified(w, view, View);
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 1);
}

void Effects_effect_without_a_shader_is_skipped(void)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .color = true, .scale = 1 });
    add_effect(view, e, true);
    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_view_only_runs_registered_effects(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader, .params = grade_params,
        .color = true, .scale = 1 });
    set_grade(a, 1, 1);
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader, .params = grade_params,
        .color = true, .scale = 1 });
    set_grade(b, 2, 1);
    add_effect(view, b, true);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 1);
    test_int(packet.view.effect_count, 1);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 0))->params[0], 2);
}

void Effects_view_without_a_list_runs_no_effects(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 0);
}

void Effects_disabled_entry_skips_the_chain(void)
{
    ecs_entity_t effect = ecs_new(w);
    ecs_set(w, effect, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    add_effect(view, effect, false);
    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
    test_int(packet.view.effect_count, 0);
}

void Effects_inputs_are_the_union_of_the_passes(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader,
        .color = true, .depth = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader,
        .color = true, .normals = true, .motion = true,
        .scale = 1 });
    add_effect(view, a, true);
    add_effect(view, b, true);
    box_render_extract(w, &packet);
    test_int(packet.view.effect_inputs,
        BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH | BOX_EFFECT_NORMALS | BOX_EFFECT_MOTION);
}

void Effects_disabled_pass_does_not_ask_for_its_inputs(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader,
        .color = true, .normals = true, .scale = 1 });
    add_effect(view, a, true);
    add_effect(view, b, false);
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 1);
    test_int(packet.view.effect_inputs, BOX_EFFECT_COLOR);
}

void Effects_parameters_come_from_the_parameter_component(void)
{
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Effect, { .shader = fx_shader, .params = grade_params,
        .color = true, .scale = 1 });
    set_grade(e, 0.75f, 1.5f);
    add_effect(view, e, true);
    box_render_extract(w, &packet);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->params[0], 0.75f);
    test_flt(pass->params[1], 1.5f);
}

void Effects_two_passes_of_one_scale_share_a_target_size(void)
{
    uint32_t aw, ah, bw, bh;
    box_effect_size(640, 480, 1, &aw, &ah);
    box_effect_size(640, 480, 1, &bw, &bh);
    test_uint(aw, bw);
    test_uint(ah, bh);
}

void Effects_half_scale_pass_asks_for_half_the_target(void)
{
    uint32_t width, height;
    box_effect_size(640, 480, 0.5f, &width, &height);
    test_uint(width, 320);
    test_uint(height, 240);
    box_effect_size(1, 1, 0.5f, &width, &height);
    test_uint(width, 1);
    test_uint(height, 1);
}

void Effects_two_views_keep_their_own_range(void)
{
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Effect, { .shader = fx_shader,
        .color = true, .scale = 1 });
    add_effect(view, a, true);
    add_effect(view, b, true);
    ecs_entity_t cam = ecs_get(w, view, View)->camera;
    ecs_entity_t second = ecs_new(w);
    ecs_add(w, second, View);
    ecs_get_mut(w, second, View)->camera = cam;
    ecs_get_mut(w, second, View)->priority = 1;
    add_effect(second, a, true);
    box_render_extract(w, &packet);
    test_int(packet.views.count, 2);
    box_view_t *v0 = box_vec_at(&packet.views, 0);
    box_view_t *v1 = box_vec_at(&packet.views, 1);
    test_int(v0->effect_first, 0);
    test_int(v0->effect_count, 2);
    test_int(v1->effect_first, 2);
    test_int(v1->effect_count, 1);
}
