/**
 * @file Wear.c
 * @brief Tests for the per entity material layer amounts packed into an instance.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Wear_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
}

void Wear_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Wear_an_entity_without_it_reads_the_neutral_values(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_worn(&u->data), false);
    test_int(u->data.wear[0], 0);
    test_int(u->data.wear[1], 0);
    test_flt(box_instance_wear(&u->data, 0), 1);
    test_flt(box_instance_wear(&u->data, 5), 1);
    test_flt(box_instance_wear(&u->data, 6), 0);
    test_flt(box_instance_wear(&u->data, 7), 1);
    test_bool(box_render_state(w)->worn, false);
}

void Wear_the_amounts_reach_the_packet(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Wear, {.dust = 1, .grime = 0.6f, .edge = 0.2f, .wet = 0});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_bool(box_render_state(w)->worn, true);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_worn(&u->data), true);
    test_flt(box_instance_wear(&u->data, 0), 1);
    test_assert(fabsf(box_instance_wear(&u->data, 1) - 0.6f) < 0.01f);
    test_assert(fabsf(box_instance_wear(&u->data, 2) - 0.2f) < 0.01f);
    test_flt(box_instance_wear(&u->data, 3), 0);
    test_flt(box_instance_wear(&u->data, 4), 1);
    test_flt(box_instance_wear(&u->data, 6), 0);
    test_flt(box_instance_wear(&u->data, 7), 1);
}

void Wear_the_detail_carries_the_seed_and_the_scale(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, WearDetail, {.burn = 0.5f, .scratch = 0, .seed = 0.8f, .scale = 2});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_worn(&u->data), true);
    test_flt(box_instance_wear(&u->data, 0), 1);
    test_assert(fabsf(box_instance_wear(&u->data, 4) - 0.5f) < 0.01f);
    test_flt(box_instance_wear(&u->data, 5), 0);
    test_assert(fabsf(box_instance_wear(&u->data, 6) - 0.8f) < 0.01f);
    test_assert(fabsf(box_instance_wear(&u->data, 7) - 2.0f) < 0.02f);
}

void Wear_the_values_are_clamped_to_their_range(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Wear, {.dust = 4, .grime = -1, .edge = 1, .wet = 1});
    ecs_set(w, e, WearDetail, {.burn = 1, .scratch = 1, .seed = 0, .scale = 40});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(box_instance_wear(&u->data, 0), 1);
    test_flt(box_instance_wear(&u->data, 1), 0);
    test_assert(fabsf(box_instance_wear(&u->data, 7) - BOX_WEAR_SCALE_MAX) < 1e-6f);
}

void Wear_a_change_reextracts_the_instance(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Wear, {.dust = 1, .grime = 1, .edge = 1, .wet = 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);

    box_packet_reset(&packet);
    ecs_set(w, e, Wear, {.dust = 0, .grime = 1, .edge = 1, .wet = 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(box_instance_wear(&u->data, 0), 0);
}

void Wear_the_amounts_survive_a_slot_move(void)
{
    // ore_engine/instance_wear
    ecs_entity_t a = ecs_new(w);
    ecs_set(w, a, Position, {0, 0, 0});
    ecs_set(w, a, Box, {1, 1, 1});
    ecs_entity_t b = ecs_new(w);
    ecs_set(w, b, Position, {2, 0, 0});
    ecs_set(w, b, Box, {1, 1, 1});
    ecs_set(w, b, Wear, {.dust = 0, .grime = 1, .edge = 1, .wet = 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(ecs_get(w, b, RenderInstance)->slot, 1);

    box_packet_reset(&packet);
    ecs_delete(w, a);
    box_render_extract(w, &packet);
    test_int(ecs_get(w, b, RenderInstance)->slot, 0);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_int(u->slot, 0);
    test_bool(box_instance_worn(&u->data), true);
    test_flt(box_instance_wear(&u->data, 0), 0);
}

void Wear_an_instance_of_a_prefab_inherits_it(void)
{
    // ore_engine/instance_wear
    ecs_entity_t prefab = ecs_new(w);
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Box, {1, 1, 1});
    ecs_set(w, prefab, Wear, {.dust = 0.25f, .grime = 1, .edge = 1, .wet = 1});

    ecs_entity_t e = ecs_new(w);
    ecs_add_pair(w, e, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_bool(box_instance_worn(&u->data), true);
    test_assert(fabsf(box_instance_wear(&u->data, 0) - 0.25f) < 0.01f);
}

void Wear_a_dynamic_instance_carries_its_amounts(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_id(w, e, DynamicTransform);
    ecs_set(w, e, Wear, {.dust = 0.5f, .grime = 1, .edge = 1, .wet = 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.dynamic_instances.count, 1);
    box_instance_t *data = box_vec_at(&packet.dynamic_instances, 0);
    test_bool(box_instance_worn(data), true);
    test_assert(fabsf(box_instance_wear(data, 0) - 0.5f) < 0.01f);
}

void Wear_a_transition_keeps_the_instance_extracted(void)
{
    // ore_engine/instance_wear
    test_int(ecs_script_run(w, "test",
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.transition\n"
        "e {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {1, 1, 1}\n"
        "  Wear: {dust: 0}\n"
        "  Transition: [Wear: {duration: 1}]\n"
        "}\n", NULL), 0);
    ecs_entity_t e = ecs_lookup(w, "e");
    test_assert(e != 0);
    box_render_extract(w, &packet);
    uint32_t i;
    for (i = 0; i < packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);

        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_reset(&packet);
    ecs_set(w, e, Wear, {.dust = 1, .grime = 1, .edge = 1, .wet = 1});
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    test_int(packet.transitions.count, 0);

    box_packet_reset(&packet);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
}

void Wear_the_layers_of_the_shader_read_the_channel(void)
{
    // ore_engine/instance_wear
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_pair(w, e, MaterialLayerOn, ecs_lookup(w, "box.material.layers.dust"));
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    box_shader_cmd_t *shader = box_vec_at(&packet.shader_create, 0);
    test_uint(shader->handle, BOX_SHADER_MESH);
    test_assert(strstr(shader->source, "(*s).wear.dust") != NULL);
    test_assert(strstr(shader->source, "(*s).wear.grime") != NULL);
    test_assert(strstr(shader->source, "(*s).wear.edge") != NULL);
    test_assert(strstr(shader->source, "(*s).wear.wet") != NULL);
    test_assert(strstr(shader->source, "fn wear_noise(") != NULL);
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }
}
