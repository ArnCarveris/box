/**
 * @file Material.c
 * @brief Tests for tint resolution and material data in instances.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Material_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
}

void Material_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Material_tint_resolves_to_children(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, parent, Tint, {255, 128, 0, 255});
    test_assert(ecs_get(w, parent, ActualTint) != NULL);
    test_int(ecs_get(w, child, ActualTint)->g, 128);
    test_int(ecs_get(w, grandchild, ActualTint)->r, 255);
}

void Material_child_with_own_tint_keeps_it(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    ecs_set(w, parent, Tint, {255, 128, 0, 255});
    test_int(ecs_get(w, child, ActualTint)->r, 1);
    test_int(ecs_get(w, child, ActualTint)->a, 4);
}

void Material_tint_removed_falls_back_to_parent(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    test_int(ecs_get(w, child, ActualTint)->r, 1);
    ecs_remove(w, child, Tint);
    test_int(ecs_get(w, child, ActualTint)->r, 10);
    ecs_remove(w, parent, Tint);
    test_assert(ecs_get(w, parent, ActualTint) == NULL);
    test_assert(ecs_get(w, child, ActualTint) == NULL);
}

void Material_new_child_inherits_tint(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    test_assert(ecs_get(w, child, ActualTint) != NULL);
    test_int(ecs_get(w, child, ActualTint)->b, 30);
}

void Material_instance_color_uses_tint_and_opacity(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Tint, {255, 0, 255, 128});
    ecs_entity_t e = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {255, 255, 255});
    ecs_set(w, e, Opacity, {0.5f});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_transparent_t *u = box_vec_at(&packet.transparent, 0);
    test_flt(u->data.color[0], 1.0f);
    test_flt(u->data.color[1], 0.0f);
    test_flt(u->data.color[2], 1.0f);
    test_assert(fabsf(u->data.color[3] - 0.5f * 128.0f / 255.0f) < 1e-5f);
}

void Material_emissive_scaled_by_strength(void)
{
    // scene_core/material
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Emissive, {.strength = 3, .color = {255, 255, 0}});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.emissive[0], 3.0f);
    test_flt(u->data.emissive[1], 3.0f);
    test_flt(u->data.emissive[2], 0.0f);
}

void Material_pbr_defaults(void)
{
    // scene_core/material
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add(w, e, PbrMaterial);
    test_flt(ecs_get(w, e, PbrMaterial)->roughness, 0.5f);
    test_flt(ecs_get(w, e, PbrMaterial)->metallic, 0.0f);
    ecs_add(w, e, Opacity);
    test_flt(ecs_get(w, e, Opacity)->value, 1.0f);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.params[1], 0.5f);
    test_flt(u->data.color[0], 1.0f);
}
