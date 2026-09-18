/**
 * @file Effects.c
 * @brief Tests for the post process chain of a view: order, inputs and targets.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/renderer/bloom.h"
#include "../../../src/renderer/gtao.h"
#include "../../../src/renderer/ssr.h"
#include "../../../src/renderer/fog.h"
#include "../../../src/renderer/exposure.h"

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

void Effects_histogram_bins_map_the_log_range(void)
{
    // effects/exposure_tonemap
    test_uint(box_exposure_bin(0.0f), 0);
    test_uint(box_exposure_bin(BOX_EXPOSURE_BLACK * 0.5f), 0);
    test_uint(box_exposure_bin(exp2f(BOX_EXPOSURE_MIN_LOG)), 1);
    test_uint(box_exposure_bin(exp2f(BOX_EXPOSURE_MIN_LOG - 4.0f)), 1);
    test_uint(box_exposure_bin(exp2f(BOX_EXPOSURE_MIN_LOG + BOX_EXPOSURE_LOG_RANGE)),
        BOX_EXPOSURE_BINS - 1);
    test_uint(box_exposure_bin(exp2f(BOX_EXPOSURE_MIN_LOG + BOX_EXPOSURE_LOG_RANGE + 8.0f)),
        BOX_EXPOSURE_BINS - 1);
    test_uint(box_exposure_bin(1.0f), 160);
    test_assert(fabsf(box_exposure_luma((float)box_exposure_bin(1.0f)) - 1.0f) < 0.02f);
    test_assert(fabsf(box_exposure_luma((float)box_exposure_bin(0.25f)) - 0.25f) < 0.01f);
}

void Effects_histogram_average_skips_the_black_bin(void)
{
    // effects/exposure_tonemap
    uint32_t bins[BOX_EXPOSURE_BINS] = {0};
    test_flt(box_exposure_average(bins, BOX_EXPOSURE_BINS), 0.0f);

    bins[0] = 5000;
    test_flt(box_exposure_average(bins, BOX_EXPOSURE_BINS), 0.0f);

    bins[box_exposure_bin(1.0f)] = 10;
    test_assert(fabsf(box_exposure_average(bins, BOX_EXPOSURE_BINS) - 1.0f) < 0.02f);

    bins[box_exposure_bin(1.0f)] = 0;
    bins[box_exposure_bin(0.125f)] = 4;
    bins[box_exposure_bin(2.0f)] = 4;
    test_assert(fabsf(box_exposure_average(bins, BOX_EXPOSURE_BINS) - 0.5f) < 0.02f);
}

void Effects_exposure_adapts_at_the_speed_of_its_direction(void)
{
    // effects/exposure_tonemap
    float up = box_exposure_step(0.0f, 0.09f, 0.18f, 4.0f, 1.0f, -8.0f, 8.0f, false, 0.1f);
    test_flt(up, 0.4f);

    float down = box_exposure_step(2.0f, 0.36f, 0.18f, 4.0f, 1.0f, -8.0f, 8.0f, false, 0.1f);
    test_flt(down, 1.9f);

    float reached = box_exposure_step(1.0f, 0.09f, 0.18f, 4.0f, 1.0f, -8.0f, 8.0f, false, 1.0f);
    test_flt(reached, 1.0f);

    float still = box_exposure_step(1.0f, 0.09f, 0.18f, 4.0f, 1.0f, -8.0f, 8.0f, false, 0.0f);
    test_flt(still, 1.0f);
}

void Effects_exposure_stays_inside_its_range(void)
{
    // effects/exposure_tonemap
    float high = box_exposure_step(0.0f, 0.001f, 0.18f, 100.0f, 100.0f, -2.0f, 2.0f, false,
        1.0f);
    test_flt(high, 2.0f);

    float low = box_exposure_step(0.0f, 100.0f, 0.18f, 100.0f, 100.0f, -2.0f, 2.0f, false,
        1.0f);
    test_flt(low, -2.0f);

    float outside = box_exposure_step(6.0f, 0.18f, 0.18f, 0.0f, 0.0f, -2.0f, 2.0f, false,
        1.0f);
    test_flt(outside, 2.0f);

    float pulled = box_exposure_step(6.0f, 0.18f, 0.18f, 1.0f, 1.0f, -2.0f, 2.0f, false,
        1.0f);
    test_flt(pulled, 1.0f);
    test_flt(box_exposure_scale(2.0f, 0.0f), 4.0f);
    test_flt(box_exposure_scale(0.0f, -1.0f), 0.5f);
}

void Effects_pinned_exposure_keeps_its_value(void)
{
    // effects/exposure_tonemap
    float pinned = box_exposure_step(1.5f, 0.001f, 0.18f, 100.0f, 100.0f, -8.0f, 8.0f, true,
        1.0f);
    test_flt(pinned, 1.5f);

    float clamped = box_exposure_step(9.0f, 0.001f, 0.18f, 100.0f, 100.0f, -8.0f, 8.0f, true,
        1.0f);
    test_flt(clamped, 8.0f);
}

void Effects_builtin_exposure_and_tonemap_are_off(void)
{
    // effects/exposure_tonemap
    ecs_entity_t exposure = ecs_lookup(w, "box.effects.stack.auto_exposure");
    ecs_entity_t tonemap = ecs_lookup(w, "box.effects.stack.tonemap");
    test_assert(exposure != 0);
    test_assert(tonemap != 0);

    const Effect *fx = ecs_get(w, exposure, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_bool(fx->exposure, true);
    test_int(fx->order, 850);
    test_uint(fx->shader, ecs_lookup(w, "box.effects.shaders.auto_exposure"));

    const Effect *map = ecs_get(w, tonemap, Effect);
    test_assert(map != NULL);
    test_bool(map->enabled, false);
    test_int(map->order, 900);
    test_assert(map->order > fx->order);

    const AutoExposure *params = ecs_get(w, exposure, AutoExposure);
    test_assert(params != NULL);
    test_flt(params->target, 0.18f);
    test_bool(params->pinned, false);
    const ToneMapping *curve = ecs_get(w, tonemap, ToneMapping);
    test_assert(curve != NULL);
    test_flt(curve->white, 1.0f);
    test_bool(curve->dither, true);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_tonemap_parameters_reach_the_pass(void)
{
    // effects/exposure_tonemap
    ecs_entity_t tonemap = ecs_lookup(w, "box.effects.stack.tonemap");
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    ecs_set(w, tonemap, ToneMapping, { .white = 2.5f, .dither = false });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->params[0], 2.5f);
    test_flt(pass->params[1], 0.0f);
    test_int(pass->inputs, BOX_EFFECT_COLOR);

    box_packet_reset(&packet);
    ecs_set(w, tonemap, ToneMapping, { .white = 1.0f, .dither = true });
    box_render_extract(w, &packet);
    test_flt(((box_effect_t*)box_vec_at(&packet.effects, 0))->params[1], 1.0f);
}

void Effects_exposure_pass_declares_its_input(void)
{
    // effects/exposure_tonemap
    ecs_entity_t exposure = ecs_lookup(w, "box.effects.stack.auto_exposure");
    ecs_get_mut(w, exposure, Effect)->enabled = true;
    ecs_modified(w, exposure, Effect);
    ecs_set(w, exposure, AutoExposure, { .speed_up = 2.0f, .speed_down = 3.0f,
        .min_ev = -5.0f, .max_ev = 6.0f, .target = 0.4f, .compensation = 0.5f,
        .pinned = true });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_int(pass->inputs, BOX_EFFECT_COLOR | BOX_EFFECT_EXPOSURE);
    test_int(packet.view.effect_inputs, BOX_EFFECT_COLOR | BOX_EFFECT_EXPOSURE);
    test_flt(pass->params[0], 2.0f);
    test_flt(pass->params[1], 3.0f);
    test_flt(pass->params[2], -5.0f);
    test_flt(pass->params[3], 6.0f);
    test_flt(pass->params[4], 0.4f);
    test_flt(pass->params[5], 0.5f);
    test_flt(pass->params[6], 1.0f);
}

void Effects_bloom_pyramid_stops_above_the_minimum(void)
{
    // effects/bloom
    test_uint(box_bloom_chain(1280, 720), 6);
    test_uint(box_bloom_chain(1920, 1080), 7);
    test_uint(box_bloom_chain(641, 361), 5);
    test_uint(box_bloom_chain(16, 16), 1);
    test_uint(box_bloom_chain(1, 1), 1);
    test_uint(box_bloom_chain(8192, 8192), BOX_BLOOM_LEVELS_MAX);
}

void Effects_bloom_levels_follow_the_radius(void)
{
    // effects/bloom
    test_uint(box_bloom_levels(1280, 720, 1.0f), 6);
    test_uint(box_bloom_levels(1280, 720, 0.5f), 4);
    test_uint(box_bloom_levels(1280, 720, 0.0f), 1);
    test_uint(box_bloom_levels(1280, 720, -3.0f), 1);
    test_uint(box_bloom_levels(1280, 720, 4.0f), 6);
    test_uint(box_bloom_levels(16, 16, 1.0f), 1);
}

void Effects_karis_weight_falls_with_the_luminance(void)
{
    // effects/bloom
    test_flt(box_bloom_karis(0.0f), 1.0f);
    test_flt(box_bloom_karis(-2.0f), 1.0f);
    test_flt(box_bloom_karis(1.0f), 0.5f);
    test_flt(box_bloom_karis(3.0f), 0.25f);
    test_assert(box_bloom_karis(100.0f) < box_bloom_karis(10.0f));
    test_assert(box_bloom_karis(100.0f) > 0.0f);
}

void Effects_builtin_bloom_is_off_by_default(void)
{
    // effects/bloom
    ecs_entity_t bloom = ecs_lookup(w, "box.effects.stack.bloom");
    test_assert(bloom != 0);
    const Effect *fx = ecs_get(w, bloom, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_bool(fx->pyramid, true);
    test_bool(fx->color, true);
    test_int(fx->order, 800);
    test_assert(fx->order < ecs_get(w, ecs_lookup(w, "box.effects.stack.tonemap"), Effect)->order);
    test_uint(fx->shader, ecs_lookup(w, "box.effects.shaders.bloom"));

    const Bloom *params = ecs_get(w, bloom, Bloom);
    test_assert(params != NULL);
    test_flt(params->intensity, 0.08f);
    test_flt(params->radius, 0.7f);
    test_flt(params->knee, 0.0f);
    test_flt(params->firefly_clamp, 8.0f);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_bloom_parameters_reach_the_pass(void)
{
    // effects/bloom
    ecs_entity_t bloom = ecs_lookup(w, "box.effects.stack.bloom");
    ecs_get_mut(w, bloom, Effect)->enabled = true;
    ecs_modified(w, bloom, Effect);
    ecs_set(w, bloom, Bloom, { .intensity = 0.3f, .radius = 0.5f, .knee = 1.25f,
        .firefly_clamp = 12.0f });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_int(pass->inputs, BOX_EFFECT_COLOR | BOX_EFFECT_PYRAMID);
    test_int(packet.view.effect_inputs, BOX_EFFECT_COLOR | BOX_EFFECT_PYRAMID);
    test_flt(pass->params[0], 0.3f);
    test_flt(pass->params[1], 0.5f);
    test_flt(pass->params[2], 1.25f);
    test_flt(pass->params[3], 12.0f);
    test_uint(box_bloom_chain(packet.view.width, packet.view.height), 5);
    test_uint(box_bloom_levels(packet.view.width, packet.view.height, pass->params[1]), 3);
}

void Effects_fog_integrates_a_level_ray(void)
{
    // effects/height_fog
    test_flt(box_fog_depth(0.1f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, 10.0f), 1.0f);
    test_assert(fabsf(box_fog_depth(0.1f, 0.5f, 0.0f, 2.0f, 0.0f, 0.0f, 10.0f) -
        0.367879f) < 0.0001f);
    test_assert(fabsf(box_fog_depth(0.1f, 0.5f, 2.0f, 2.0f, 0.0f, 0.0f, 10.0f) - 1.0f) <
        0.0001f);
    test_flt(box_fog_depth(0.1f, 0.0f, 0.0f, 5.0f, 0.0f, 4.0f, 4.0f), 0.0f);
    test_flt(box_fog_depth(0.1f, 0.0f, 0.0f, 5.0f, 0.0f, 8.0f, 4.0f), 0.0f);
    test_flt(box_fog_depth(0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f), 0.0f);
}

void Effects_fog_integral_matches_a_numeric_one(void)
{
    // effects/height_fog
    float density = 0.08f;
    float falloff = 0.35f;
    float height = 1.5f;
    float y0 = 4.0f;
    float t0 = 3.0f;
    float t1 = 40.0f;
    float dy;
    for (dy = -0.9f; dy < 0.95f; dy += 0.3f) {
        double numeric = 0.0;
        int32_t steps = 20000;
        int32_t i;
        for (i = 0; i < steps; i ++) {
            double t = t0 + ((double)i + 0.5) * (t1 - t0) / steps;
            numeric += density * exp(-falloff * (y0 + dy * t - height)) * (t1 - t0) / steps;
        }

        float analytic = box_fog_depth(density, falloff, height, y0, dy, t0, t1);
        test_assert(fabs(numeric - analytic) < 0.0005 * (numeric + 0.01));
    }
}

void Effects_fog_thins_as_the_ray_climbs(void)
{
    // effects/height_fog
    float level = box_fog_depth(0.2f, 0.4f, 0.0f, 2.0f, 0.0f, 0.0f, 30.0f);
    float up = box_fog_depth(0.2f, 0.4f, 0.0f, 2.0f, 0.6f, 0.0f, 30.0f);
    float down = box_fog_depth(0.2f, 0.4f, 0.0f, 2.0f, -0.06f, 0.0f, 30.0f);
    test_assert(up < level);
    test_assert(down > level);
    test_assert(up > 0.0f);

    float high = box_fog_depth(0.2f, 0.4f, 0.0f, 20.0f, 0.0f, 0.0f, 30.0f);
    test_assert(high < level);
}

void Effects_fog_factor_is_capped(void)
{
    // effects/height_fog
    float thin = box_fog_factor(0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 1.0f);
    test_assert(fabsf(thin - 0.181269f) < 0.0001f);

    float thick = box_fog_factor(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 1.0f);
    test_assert(thick > 0.999f);

    float capped = box_fog_factor(2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.6f);
    test_flt(capped, 0.6f);

    float none = box_fog_factor(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 20.0f, 10.0f, 1.0f);
    test_flt(none, 0.0f);
    test_assert(box_fog_factor(0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f) > 0.18f);
}

void Effects_builtin_fog_is_off_by_default(void)
{
    // effects/height_fog
    ecs_entity_t fog = ecs_lookup(w, "box.effects.stack.fog");
    test_assert(fog != 0);
    const Effect *fx = ecs_get(w, fog, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_bool(fx->depth, true);
    test_bool(fx->color, true);
    test_int(fx->order, 700);
    test_assert(fx->order < ecs_get(w, ecs_lookup(w, "box.effects.stack.bloom"), Effect)->order);
    test_uint(fx->shader, ecs_lookup(w, "box.effects.shaders.fog"));

    const Fog *params = ecs_get(w, fog, Fog);
    test_assert(params != NULL);
    test_flt(params->density, 0.02f);
    test_flt(params->max_opacity, 1.0f);
    test_flt(params->height, 0.0f);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_fog_parameters_reach_the_pass(void)
{
    // effects/height_fog
    ecs_entity_t fog = ecs_lookup(w, "box.effects.stack.fog");
    ecs_get_mut(w, fog, Effect)->enabled = true;
    ecs_modified(w, fog, Effect);
    ecs_set(w, fog, Fog, { .color = {0.25f, 0.5f, 0.75f}, .density = 0.04f, .falloff = 0.3f,
        .start = 5.0f, .max_opacity = 0.8f, .height = 1.5f });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_int(pass->inputs, BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH);
    test_flt(pass->params[0], 0.25f);
    test_flt(pass->params[1], 0.5f);
    test_flt(pass->params[2], 0.75f);
    test_flt(pass->params[3], 0.04f);
    test_flt(pass->params[4], 0.3f);
    test_flt(pass->params[5], 5.0f);
    test_flt(pass->params[6], 0.8f);
    test_flt(pass->params[7], 1.5f);
}

void Effects_chain_gives_the_view_a_float_target(void)
{
    // effects/hdr_target
    box_render_extract(w, &packet);
    test_int(packet.view.effect_count, 0);
    test_bool(box_view_hdr(&packet.view), false);

    box_packet_reset(&packet);
    ecs_entity_t tonemap = ecs_lookup(w, "box.effects.stack.tonemap");
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.view.effect_count, 1);
    test_bool(box_view_hdr(&packet.view), true);
}

void Effects_float_target_moves_the_gamma_encode(void)
{
    // effects/hdr_target
    test_flt(box_view_gamma(false, false), 1.0f);
    test_flt(box_view_gamma(true, false), 0.0f);
    test_flt(box_view_gamma(false, true), 0.0f);
    test_flt(box_view_gamma(true, true), 0.0f);

    test_flt(box_effect_decode_flag(false, false), 1.0f);
    test_flt(box_effect_decode_flag(true, false), 0.0f);
    test_flt(box_effect_decode_flag(true, true), 0.0f);
}

void Effects_chain_encodes_once_at_the_end(void)
{
    // effects/hdr_target
    float encodes = 0.0f;
    int32_t i;
    for (i = 0; i < 4; i ++) {
        encodes += box_effect_encode_flag(true, false, i == 3);
    }

    test_flt(encodes, 1.0f);
    test_flt(box_effect_encode_flag(true, false, true), 1.0f);
    test_flt(box_effect_encode_flag(true, false, false), 0.0f);
    test_flt(box_effect_encode_flag(true, true, true), 0.0f);
    test_flt(box_effect_encode_flag(false, false, false), 1.0f);
}

void Effects_gtao_horizon_of_a_flat_plane(void)
{
    // effects/gtao
    float open = box_gtao_slice(0.0f, -BOX_GTAO_HALF_PI, BOX_GTAO_HALF_PI, 1.0f);
    test_assert(fabsf(open - 1.0f) < 0.0001f);

    float tilted = box_gtao_slice(0.5f, -BOX_GTAO_HALF_PI, BOX_GTAO_HALF_PI, 1.0f);
    test_assert(fabsf(tilted - 1.0f) < 0.005f);

    float wider = box_gtao_slice(0.0f, -3.0f, 3.0f, 1.0f);
    test_assert(fabsf(wider - 1.0f) < 0.0001f);
}

void Effects_gtao_horizon_of_a_corner(void)
{
    // effects/gtao
    float half = box_gtao_slice(0.0f, -BOX_GTAO_HALF_PI, 0.0f, 1.0f);
    test_assert(fabsf(half - 0.5f) < 0.0001f);

    float closed = box_gtao_slice(0.0f, 0.0f, 0.0f, 1.0f);
    test_assert(closed < 0.0001f);

    float corner = box_gtao_slice(0.0f, -0.4f, 0.4f, 1.0f);
    test_assert(corner < half);
    test_assert(corner > 0.0f);
    test_assert(box_gtao_slice(0.0f, -0.4f, 0.4f, 0.5f) < corner);
}

void Effects_gtao_runs_at_half_resolution(void)
{
    // effects/gtao
    test_uint(box_gtao_size(1280, true), 640);
    test_uint(box_gtao_size(720, true), 360);
    test_uint(box_gtao_size(1280, false), 1280);
    test_uint(box_gtao_size(1, true), 1);
}

void Effects_builtin_gtao_is_off_by_default(void)
{
    // effects/gtao
    ecs_entity_t gtao = ecs_lookup(w, "box.effects.stack.gtao");
    test_assert(gtao != 0);
    const Effect *fx = ecs_get(w, gtao, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_bool(fx->depth, true);
    test_bool(fx->normals, true);
    test_bool(fx->motion, true);
    test_bool(fx->occlusion, true);
    test_int(fx->order, 600);
    test_assert(fx->order < ecs_get(w, ecs_lookup(w, "box.effects.stack.fog"), Effect)->order);

    const Gtao *params = ecs_get(w, gtao, Gtao);
    test_assert(params != NULL);
    test_flt(params->radius, BOX_GTAO_RADIUS);
    test_flt(params->slices, (float)BOX_GTAO_SLICES);
    test_flt(params->steps, (float)BOX_GTAO_STEPS);
    test_bool(params->half_res, true);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_gtao_parameters_reach_the_pass(void)
{
    // effects/gtao
    ecs_entity_t gtao = ecs_lookup(w, "box.effects.stack.gtao");
    ecs_get_mut(w, gtao, Effect)->enabled = true;
    ecs_modified(w, gtao, Effect);
    ecs_set(w, gtao, Gtao, { .radius = 2.5f, .slices = 3.0f, .steps = 8.0f, .power = 1.5f,
        .temporal = false, .half_res = false });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_int(pass->inputs, BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH | BOX_EFFECT_NORMALS |
        BOX_EFFECT_MOTION | BOX_EFFECT_OCCLUSION);
    test_flt(pass->params[0], 2.5f);
    test_flt(pass->params[1], 3.0f);
    test_flt(pass->params[2], 8.0f);
    test_flt(pass->params[3], 1.5f);
    test_flt(pass->params[4], 0.0f);
    test_flt(pass->params[5], 0.0f);
    test_int(packet.view.effect_inputs & BOX_EFFECT_OCCLUSION, BOX_EFFECT_OCCLUSION);
}

void Effects_ssr_march_hits_a_floor(void)
{
    // effects/ssr
    float scene[6] = { 12.0f, 11.0f, 10.0f, 9.0f, 8.4f, 8.0f };
    float ray[6] = { 11.0f, 10.5f, 10.0f, 9.2f, 8.5f, 8.2f };
    test_int(box_ssr_march(scene, ray, 6, 0.4f), 3);
    test_bool(box_ssr_behind(9.2f, 9.0f, 0.4f), true);
    test_bool(box_ssr_behind(8.9f, 9.0f, 0.4f), false);
    test_bool(box_ssr_behind(20.0f, 9.0f, 0.4f), false);
}

void Effects_ssr_march_misses_off_screen(void)
{
    // effects/ssr
    float empty[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float ray[4] = { 4.0f, 8.0f, 12.0f, 16.0f };
    test_int(box_ssr_march(empty, ray, 4, 0.4f), -1);

    float behind[4] = { 20.0f, 20.0f, 20.0f, 20.0f };
    test_int(box_ssr_march(behind, ray, 4, 0.4f), -1);
}

void Effects_ssr_weight_falls_with_the_roughness(void)
{
    // effects/ssr
    test_flt(box_ssr_roughness_weight(0.0f, 0.6f), 1.0f);
    test_flt(box_ssr_roughness_weight(0.3f, 0.6f), 1.0f);
    test_flt(box_ssr_roughness_weight(0.6f, 0.6f), 0.0f);
    test_flt(box_ssr_roughness_weight(0.9f, 0.6f), 0.0f);
    float middle = box_ssr_roughness_weight(0.45f, 0.6f);
    test_assert(middle > 0.0f && middle < 1.0f);
    test_assert(box_ssr_roughness_weight(0.5f, 0.6f) < middle);

    test_flt(box_ssr_edge_fade(0.5f, 0.5f, 0.12f), 1.0f);
    test_flt(box_ssr_edge_fade(0.0f, 0.5f, 0.12f), 0.0f);
    test_flt(box_ssr_edge_fade(0.5f, 1.0f, 0.12f), 0.0f);
    float near_edge = box_ssr_edge_fade(0.06f, 0.5f, 0.12f);
    test_assert(near_edge > 0.0f && near_edge < 1.0f);
}

void Effects_builtin_ssr_is_off_by_default(void)
{
    // effects/ssr
    ecs_entity_t ssr = ecs_lookup(w, "box.effects.stack.ssr");
    test_assert(ssr != 0);
    const Effect *fx = ecs_get(w, ssr, Effect);
    test_assert(fx != NULL);
    test_bool(fx->enabled, false);
    test_bool(fx->depth, true);
    test_bool(fx->normals, true);
    test_bool(fx->occlusion, false);
    test_int(fx->order, 650);
    test_assert(fx->order > ecs_get(w, ecs_lookup(w, "box.effects.stack.gtao"), Effect)->order);
    test_assert(fx->order < ecs_get(w, ecs_lookup(w, "box.effects.stack.fog"), Effect)->order);

    const Ssr *params = ecs_get(w, ssr, Ssr);
    test_assert(params != NULL);
    test_flt(params->steps, (float)BOX_SSR_STEPS);
    test_flt(params->thickness, BOX_SSR_THICKNESS);
    test_flt(params->max_roughness, BOX_SSR_MAX_ROUGHNESS);
    test_flt(params->fade, BOX_SSR_FADE);

    box_render_extract(w, &packet);
    test_int(packet.effects.count, 0);
}

void Effects_ssr_parameters_reach_the_pass(void)
{
    // effects/ssr
    ecs_entity_t ssr = ecs_lookup(w, "box.effects.stack.ssr");
    ecs_get_mut(w, ssr, Effect)->enabled = true;
    ecs_modified(w, ssr, Effect);
    ecs_set(w, ssr, Ssr, { .steps = 32.0f, .thickness = 0.25f, .max_roughness = 0.4f,
        .intensity = 0.8f, .fade = 0.2f });
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_int(pass->inputs, BOX_EFFECT_COLOR | BOX_EFFECT_DEPTH | BOX_EFFECT_NORMALS);
    test_flt(pass->params[0], 32.0f);
    test_flt(pass->params[1], 0.25f);
    test_flt(pass->params[2], 0.4f);
    test_flt(pass->params[3], 0.8f);
    test_flt(pass->params[4], 0.2f);
}
