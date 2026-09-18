/**
 * @file Layers.c
 * @brief Tests for material layer registration, ordering and the per material bits.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static int32_t layer_errors;
static ecs_os_api_log_t layer_log_prev;

/** Log callback that counts errors instead of printing them.
 *
 * @param level Log level, -3 for an error
 * @param file Source file of the message
 * @param line Source line of the message
 * @param msg The message
 */
static void layer_log(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    (void)msg;
    layer_errors += level == -3;
}

void Layers_setup(void)
{
    layer_errors = 0;
    w = ecs_init();
    layer_log_prev = ecs_os_api.log_;
    ecs_os_api.log_ = layer_log;
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Layers_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
    ecs_os_api.log_ = layer_log_prev;
}

void Layers_built_in_layers_are_registered(void)
{
    // materials/material_layers
    ecs_entity_t wear = ecs_lookup(w, "box.material.layers.edge_wear");
    ecs_entity_t dust = ecs_lookup(w, "box.material.layers.dust");
    ecs_entity_t grime = ecs_lookup(w, "box.material.layers.grime");
    ecs_entity_t wetness = ecs_lookup(w, "box.material.layers.wetness");
    test_assert(wear != 0);
    test_assert(dust != 0);
    test_assert(grime != 0);
    test_assert(wetness != 0);

    const MaterialLayer *layer = ecs_get(w, dust, MaterialLayer);
    test_assert(layer != NULL);
    test_assert(layer->wgsl != NULL);
    test_int(layer->order, 10);
    const LayerParams *params = ecs_get(w, dust, LayerParams);
    test_assert(params != NULL);
    test_flt(params->a.w, 0.85f);
}

void Layers_registration_produces_the_ordered_command(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);

    int32_t count = 0;
    const box_layer_t *layers = box_render_layers(w, &count);
    test_int(count, 4);
    int32_t i;
    for (i = 1; i < count; i ++) {
        test_assert(layers[i].order >= layers[i - 1].order);
    }

    test_int(layers[0].order, 0);
    test_int(layers[3].order, 30);
    test_int(packet.shader_create.count, BOX_SHADER_BUILTIN_MAX + 1);
    box_shader_cmd_t *cmd = box_vec_at(&packet.shader_create, 0);
    test_uint(cmd->handle, BOX_SHADER_MESH);
    test_assert(strstr(cmd->source, "fn layer_3(") != NULL);
    test_assert(strstr(cmd->source, "fn layer_4(") == NULL);
}

void Layers_command_is_sent_once(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, BOX_SHADER_BUILTIN_MAX + 1);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, 0);
    test_int(packet.layer_count, 4);
}

void Layers_order_sorts_the_command(void)
{
    // materials/material_layers
    ecs_entity_t mine = ecs_new(w);
    ecs_set(w, mine, MaterialLayer, { .wgsl = ecs_os_strdup("(*s).albedo = p.a.rgb;\n"),
        .order = -5 });
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);

    int32_t count = 0;
    const box_layer_t *layers = box_render_layers(w, &count);
    test_int(count, 5);
    test_int(layers[0].order, -5);
    test_int(layers[1].order, 0);
    test_int(layers[4].order, 30);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), 1u << 2);
}

void Layers_unregistration_renumbers_and_resends(void)
{
    // materials/material_layers
    ecs_entity_t mine = ecs_new(w);
    ecs_set(w, mine, MaterialLayer, { .wgsl = ecs_os_strdup("(*s).albedo = p.a.rgb;\n"),
        .order = -5 });
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, BOX_SHADER_BUILTIN_MAX + 1);
    test_int(box_instance_layers(&((box_static_update_t*)box_vec_at(&packet.static_updates,
        0))->data), 1u << 2);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_reset(&packet);
    ecs_delete(w, mine);
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, 1);
    test_uint(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, 0))->handle,
        BOX_SHADER_MESH);
    int32_t count = 0;
    const box_layer_t *layers = box_render_layers(w, &count);
    test_int(count, 4);
    test_int(layers[0].order, 0);
    test_int(packet.static_updates.count, 1);
    test_int(box_instance_layers(&((box_static_update_t*)box_vec_at(&packet.static_updates,
        0))->data), 1u << 1);
}

void Layers_material_pair_sets_the_layer_bit(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.grime"));
    box_render_extract(w, &packet);

    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), 1u << 2);
    test_int(u->data.uv_mode & 0xFFFFu, 0);
}

void Layers_two_layers_set_two_bits(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.edge_wear"));
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.wetness"));
    box_render_extract(w, &packet);

    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), (1u << 0) | (1u << 3));
}

void Layers_removing_the_pair_clears_the_bit(void)
{
    // materials/material_layers
    ecs_entity_t dust = ecs_lookup(w, "box.material.layers.dust");
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, dust);
    box_render_extract(w, &packet);
    test_int(box_instance_layers(&((box_static_update_t*)box_vec_at(&packet.static_updates,
        0))->data), 1u << 1);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_reset(&packet);
    ecs_remove_pair(w, e, MaterialLayerOn, dust);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(box_instance_layers(&((box_static_update_t*)box_vec_at(&packet.static_updates,
        0))->data), 0);
}

void Layers_inherited_from_a_prefab(void)
{
    // materials/material_layers
    ecs_entity_t prefab = ecs_new_w_id(w, EcsPrefab);
    ecs_add_pair(w, prefab, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    ecs_entity_t e = ecs_new_w_pair(w, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);

    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), 1u << 1);
}

void Layers_dynamic_instance_gets_the_bit(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_id(w, e, DynamicTransform);
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.wetness"));
    box_render_extract(w, &packet);

    test_int(packet.dynamic_instances.count, 1);
    box_instance_t *data = box_vec_at(&packet.dynamic_instances, 0);
    test_int(box_instance_layers(data), 1u << 3);
}

void Layers_transparent_instance_keeps_the_bit(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.5f});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.grime"));
    box_render_extract(w, &packet);

    test_int(packet.transparent.count, 1);
    box_transparent_t *entry = box_vec_at(&packet.transparent, 0);
    test_int(box_instance_layers(&entry->data), 1u << 2);
    test_int(box_instance_blend(&entry->data), AlphaBlend);
}

void Layers_params_reach_the_packet(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);

    test_int(packet.layer_count, 4);
    test_flt(packet.layer_params[1].a[3], 0.85f);
    test_flt(packet.layer_params[1].b[0], 6.0f);
    test_flt(packet.layer_params[3].a[0], 0.05f);
}

void Layers_params_update_on_change(void)
{
    // materials/material_layers
    ecs_entity_t dust = ecs_lookup(w, "box.material.layers.dust");
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, dust);
    box_render_extract(w, &packet);
    test_flt(packet.layer_params[1].a[3], 0.85f);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_reset(&packet);
    ecs_set(w, dust, LayerParams, { .a = {0.1f, 0.2f, 0.3f, 0.4f},
        .b = {0.5f, 0.6f, 0.7f, 0.8f} });
    box_render_extract(w, &packet);
    test_int(packet.shader_create.count, 0);
    test_flt(packet.layer_params[1].a[0], 0.1f);
    test_flt(packet.layer_params[1].a[3], 0.4f);
    test_flt(packet.layer_params[1].b[2], 0.7f);
}

void Layers_settings_flag_reaches_the_view(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);
    test_int(packet.view.layers_enabled, 1);

    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_reset(&packet);
    ecs_singleton_set(w, MaterialLayerSettings, { false });
    box_render_extract(w, &packet);
    test_int(packet.view.layers_enabled, 0);
    test_int(packet.layer_count, 4);
}

void Layers_project_layer_gets_a_bit(void)
{
    // materials/material_layers
    ecs_entity_t mine = ecs_new(w);
    ecs_set(w, mine, MaterialLayer, { .wgsl = ecs_os_strdup("(*s).albedo = p.a.rgb;\n"),
        .order = 100 });
    ecs_set(w, mine, LayerParams, { .a = {1, 0, 0, 1} });
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, mine);
    box_render_extract(w, &packet);

    int32_t count = 0;
    const box_layer_t *layers = box_render_layers(w, &count);
    test_int(count, 5);
    test_int(layers[4].order, 100);
    box_shader_cmd_t *cmd = box_vec_at(&packet.shader_create, 0);
    test_assert(strstr(cmd->source,
        "fn layer_4(s: ptr<function, Surface>, p: LayerParams) {\n"
        "(*s).albedo = p.a.rgb;\n") != NULL);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), 1u << 4);
    test_flt(packet.layer_params[4].a[0], 1.0f);
}

void Layers_limit_is_an_error_logged_once(void)
{
    // materials/material_layers
    int32_t i;
    for (i = 0; i < 13; i ++) {
        ecs_entity_t extra = ecs_new(w);
        ecs_set(w, extra, MaterialLayer, { .wgsl = ecs_os_strdup("(*s).alpha = p.a.w;\n"),
            .order = 100 + i });
    }

    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);

    test_int(packet.layer_count, BOX_LAYER_MAX);
    test_int(layer_errors, 1);
    test_bool(box_render_state(w)->layers_reported, true);

    uint32_t cmd;
    for (cmd = 0; cmd < packet.shader_create.count; cmd ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, cmd))->source);
    }

    box_packet_reset(&packet);
    ecs_entity_t extra = ecs_new(w);
    ecs_set(w, extra, MaterialLayer, { .wgsl = ecs_os_strdup("(*s).alpha = p.a.w;\n"),
        .order = 200 });
    box_render_extract(w, &packet);
    test_int(packet.layer_count, BOX_LAYER_MAX);
    test_int(layer_errors, 1);
}

void Layers_scene_without_layers_keeps_the_flag_off(void)
{
    // materials/material_layers
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {200, 100, 50});
    box_render_extract(w, &packet);

    test_bool(box_render_state(w)->layered, false);
    test_int(packet.layer_count, 0);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(box_instance_layers(&u->data), 0);
}
