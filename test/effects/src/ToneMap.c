/**
 * @file ToneMap.c
 * @brief Tests for the tone map operators and the display transform lookup cube.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t tonemap;
static ecs_entity_t tonemap_params;

static void tonemap_set(
    float white,
    bool dither,
    int32_t curve)
{
    void *ptr = ecs_ensure_id(w, tonemap, tonemap_params, (size_t)ecs_get_type_info(w, tonemap_params)->size);
    ecs_value_t value = { .type = tonemap_params, .ptr = ptr };
    char expr[128];
    snprintf(expr, sizeof(expr), "{white: %g, dither: %s, curve: %d}",
        white, dither ? "true" : "false", curve);
    test_assert(ecs_expr_run(w, expr, &value, NULL) != NULL);
    ecs_modified_id(w, tonemap, tonemap_params);
}

void ToneMap_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    test_assert(box_script_load(w, BOX_EFFECTS_BUILTINS) != 0);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    tonemap = ecs_lookup(w, "box.effects.stack.tonemap");
    tonemap_params = ecs_lookup(w, "box.effects.ToneMapping");
}

void ToneMap_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    for (i = 0; i < packet.effect_texture_create.count; i ++) {
        free(((box_effect_texture_cmd_t*)box_vec_at(&packet.effect_texture_create, i))->texels);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void ToneMap_the_neutral_curve_is_the_default(void)
{
    // ore_engine/tonemap_operators
    test_assert(tonemap != 0);
    tonemap_set(1, true, 0);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->params[2], 0);
    test_assert(!(pass->inputs & BOX_EFFECT_TEXTURE));
    test_int(packet.effect_texture_create.count, 0);
}

void ToneMap_the_curve_reaches_the_parameter_block(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(2, true, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_flt(pass->params[0], 2);
    test_flt(pass->params[1], 1);
    test_flt(pass->params[2], 1);
}

void ToneMap_the_tony_pass_reads_the_lookup(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(1, false, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    box_effect_t *pass = box_vec_at(&packet.effects, 0);
    test_assert((pass->inputs & BOX_EFFECT_TEXTURE) != 0);
    test_assert((packet.view.effect_inputs & BOX_EFFECT_TEXTURE) != 0);
}

void ToneMap_the_cube_reaches_the_packet_once(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(1, false, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effect_texture_create.count, 1);
    box_effect_texture_cmd_t *cmd = box_vec_at(&packet.effect_texture_create, 0);
    test_int(cmd->dim, 48);
    test_assert(cmd->texels != NULL);
    free(cmd->texels);
    cmd->texels = NULL;

    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.effect_texture_create.count, 0);
}

void ToneMap_the_lookup_maps_black_to_black(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(1, false, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effect_texture_create.count, 1);
    box_effect_texture_cmd_t *cmd = box_vec_at(&packet.effect_texture_create, 0);
    float dim = (float)cmd->dim;
    float uv = 0.0f * ((dim - 1.0f) / dim) + 0.5f / dim;
    int32_t c = (int32_t)(uv * dim);
    uint32_t texel = cmd->texels[c + c * cmd->dim + c * cmd->dim * cmd->dim];
    float scale = ldexpf(1.0f, (int32_t)(texel >> 27) - 24);
    float r = (float)(texel & 0x1ff) * scale;
    float g = (float)((texel >> 9) & 0x1ff) * scale;
    float b = (float)((texel >> 18) & 0x1ff) * scale;
    test_assert(r < 0.005f);
    test_assert(g < 0.005f);
    test_assert(b < 0.005f);
}

void ToneMap_the_lookup_rises_with_the_stimulus(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(1, false, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effect_texture_create.count, 1);
    box_effect_texture_cmd_t *cmd = box_vec_at(&packet.effect_texture_create, 0);
    float dim = (float)cmd->dim;
    const float stimulus[4] = { 0.05f, 0.18f, 1.0f, 8.0f };
    float mapped[4];
    int32_t i;
    for (i = 0; i < 4; i ++) {
        float encoded = stimulus[i] / (stimulus[i] + 1.0f);
        float uv = encoded * ((dim - 1.0f) / dim) + 0.5f / dim;
        int32_t c = (int32_t)(uv * dim);
        uint32_t texel = cmd->texels[c + c * cmd->dim + c * cmd->dim * cmd->dim];
        float scale = ldexpf(1.0f, (int32_t)(texel >> 27) - 24);
        mapped[i] = (float)(texel & 0x1ff) * scale;
    }

    test_assert(mapped[0] < mapped[1]);
    test_assert(mapped[1] < mapped[2]);
    test_assert(mapped[2] < mapped[3]);
    test_assert(mapped[3] < 1.001f);
    test_assert(mapped[1] > 0.1f);
    test_assert(mapped[1] < 0.3f);
}

void ToneMap_the_lookup_desaturates_a_bright_color(void)
{
    // ore_engine/tonemap_operators
    tonemap_set(1, false, 1);
    ecs_get_mut(w, tonemap, Effect)->enabled = true;
    ecs_modified(w, tonemap, Effect);
    box_render_extract(w, &packet);

    test_int(packet.effect_texture_create.count, 1);
    box_effect_texture_cmd_t *cmd = box_vec_at(&packet.effect_texture_create, 0);
    float dim = (float)cmd->dim;
    const float stimulus[3] = { 8.0f, 0.0f, 0.0f };
    int32_t coord[3];
    int32_t i;
    for (i = 0; i < 3; i ++) {
        float encoded = stimulus[i] / (stimulus[i] + 1.0f);
        float uv = encoded * ((dim - 1.0f) / dim) + 0.5f / dim;
        coord[i] = (int32_t)(uv * dim);
    }

    uint32_t texel = cmd->texels[coord[0] + coord[1] * cmd->dim +
        coord[2] * cmd->dim * cmd->dim];
    float scale = ldexpf(1.0f, (int32_t)(texel >> 27) - 24);
    float r = (float)(texel & 0x1ff) * scale;
    float g = (float)((texel >> 9) & 0x1ff) * scale;
    float b = (float)((texel >> 18) & 0x1ff) * scale;
    test_assert(r > 0.9f);
    test_assert(g > 0.2f);
    test_assert(b > 0.15f);
    test_assert(g < r);
    test_assert(b < g);
}
