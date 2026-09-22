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
    ECS_IMPORT(w, Box);
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

void Material_child_tint_composes_with_the_parent(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    ecs_set(w, parent, Tint, {255, 128, 0, 255});
    test_int(ecs_get(w, child, ActualTint)->r, 251);
    test_int(ecs_get(w, child, ActualTint)->a, 255);
}

void Material_tint_removed_falls_back_to_parent(void)
{
    // scene_core/material
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    test_int(ecs_get(w, child, ActualTint)->r, 9);
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
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
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
    test_assert(fabsf(u->data.color[1] - (1.0f - 128.0f / 255.0f)) < 1e-5f);
    test_flt(u->data.color[2], 1.0f);
    test_flt(u->data.color[3], 0.5f);
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

void Material_tint_resolves_through_parent_component(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_entity_t grandchild = ecs_new_w_parent(w, child, "grandchild");
    ecs_set(w, parent, Tint, {255, 128, 0, 255});
    test_assert(ecs_get(w, parent, ActualTint) != NULL);
    test_int(ecs_get(w, child, ActualTint)->g, 128);
    test_int(ecs_get(w, grandchild, ActualTint)->r, 255);
}

void Material_tint_change_propagates_through_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_set(w, parent, Tint, {1, 2, 3, 4});
    test_int(ecs_get(w, child, ActualTint)->r, 1);
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    test_int(ecs_get(w, child, ActualTint)->r, 10);
    test_int(ecs_get(w, child, ActualTint)->b, 30);
}

void Material_parent_child_tint_composes_with_the_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_entity_t grandchild = ecs_new_w_parent(w, child, "grandchild");
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    ecs_set(w, parent, Tint, {255, 128, 0, 255});
    test_int(ecs_get(w, child, ActualTint)->r, 251);
    test_int(ecs_get(w, grandchild, ActualTint)->r, 251);
    test_int(ecs_get(w, parent, ActualTint)->r, 255);
}

void Material_new_parent_child_inherits_tint(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    test_assert(ecs_get(w, child, ActualTint) != NULL);
    test_int(ecs_get(w, child, ActualTint)->b, 30);
}

void Material_tint_resolves_through_mixed_tree(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t root = ecs_new(w);
    ecs_entity_t node = ecs_new_w_parent(w, root, "node");
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, node);
    ecs_entity_t tip = ecs_new_w_parent(w, leaf, "tip");
    ecs_set(w, root, Tint, {7, 8, 9, 10});
    test_int(ecs_get(w, node, ActualTint)->r, 7);
    test_int(ecs_get(w, leaf, ActualTint)->g, 8);
    test_int(ecs_get(w, tip, ActualTint)->b, 9);
    ecs_set(w, leaf, Tint, {1, 1, 1, 1});
    test_int(ecs_get(w, tip, ActualTint)->r, 6);
    test_int(ecs_get(w, node, ActualTint)->r, 7);
}

void Material_tint_removed_falls_back_through_parent(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_set(w, parent, Tint, {10, 20, 30, 40});
    ecs_set(w, child, Tint, {1, 2, 3, 4});
    test_int(ecs_get(w, child, ActualTint)->r, 9);
    ecs_remove(w, child, Tint);
    test_int(ecs_get(w, child, ActualTint)->r, 10);
    ecs_remove(w, parent, Tint);
    test_assert(ecs_get(w, parent, ActualTint) == NULL);
    test_assert(ecs_get(w, child, ActualTint) == NULL);
}

void Material_tint_follows_reparent_between_storages(void)
{
    // scene_core/parent_hierarchy
    ecs_entity_t red = ecs_new(w);
    ecs_set(w, red, Tint, {255, 0, 0, 255});
    ecs_entity_t blue = ecs_new(w);
    ecs_set(w, blue, Tint, {0, 0, 255, 255});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, red);
    test_int(ecs_get(w, child, ActualTint)->r, 255);
    ecs_set(w, child, EcsParent, { blue });
    test_int(ecs_get(w, child, ActualTint)->b, 255);
    ecs_add_pair(w, child, EcsChildOf, red);
    test_int(ecs_get(w, child, ActualTint)->r, 255);
}

void Material_tint_mix_at_zero_keeps_the_color(void)
{
    // materials/tint_compose
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {200, 100, 50});
    ecs_set(w, e, Tint, {0, 0, 255, 0});
    test_int(ecs_get(w, e, ActualTint)->a, 0);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.color[0], box_srgb_to_linear(200));
    test_flt(u->data.color[1], box_srgb_to_linear(100));
    test_flt(u->data.color[2], box_srgb_to_linear(50));
}

void Material_tint_mix_at_half_blends_the_color(void)
{
    // materials/tint_compose
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {255, 255, 255});
    ecs_set(w, e, Tint, {0, 0, 255, 128});
    test_int(ecs_get(w, e, ActualTint)->b, 255);
    test_int(ecs_get(w, e, ActualTint)->a, 128);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_assert(fabsf(u->data.color[0] - (1.0f - 128.0f / 255.0f)) < 1e-5f);
    test_assert(fabsf(u->data.color[1] - (1.0f - 128.0f / 255.0f)) < 1e-5f);
    test_flt(u->data.color[2], 1.0f);
}

void Material_tint_mix_at_full_replaces_the_color(void)
{
    // materials/tint_compose
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Rgb, {255, 0, 0});
    ecs_set(w, e, Tint, {0, 0, 255, 255});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t *u = box_vec_at(&packet.static_updates, 0);
    test_flt(u->data.color[0], 0.0f);
    test_flt(u->data.color[1], 0.0f);
    test_flt(u->data.color[2], 1.0f);
}

void Material_tint_composes_two_levels(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    const ActualTint *composed = ecs_get(w, child, ActualTint);
    test_int(composed->r, 0);
    test_int(composed->g, 0);
    test_int(composed->b, 85);
    test_int(composed->a, 192);
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
    test_int(ecs_get(w, parent, ActualTint)->b, 255);
    test_int(ecs_get(w, parent, ActualTint)->a, 128);
}

void Material_tint_composes_three_levels(void)
{
    // materials/tint_compose
    ecs_entity_t root = ecs_new(w);
    ecs_entity_t mid = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, mid);
    ecs_set(w, root, Tint, {0, 0, 255, 128});
    ecs_set(w, mid, Tint, {0, 0, 0, 128});
    ecs_set(w, leaf, Tint, {255, 255, 255, 128});
    const ActualTint *composed = ecs_get(w, leaf, ActualTint);
    test_int(composed->r, 146);
    test_int(composed->g, 146);
    test_int(composed->b, 182);
    test_int(composed->a, 224);
}

void Material_child_tint_over_an_untinted_parent(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, child, Tint, {0, 0, 255, 128});
    test_assert(ecs_get(w, parent, ActualTint) == NULL);
    test_int(ecs_get(w, child, ActualTint)->b, 255);
    test_int(ecs_get(w, child, ActualTint)->a, 128);
    test_int(ecs_get(w, grandchild, ActualTint)->b, 255);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 128);
}

void Material_root_tint_change_recomposes_the_subtree(void)
{
    // materials/tint_compose
    ecs_entity_t root = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, root, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    ecs_set(w, root, Tint, {255, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->r, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->b, 0);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
}

void Material_middle_tint_change_recomposes_below_it(void)
{
    // materials/tint_compose
    ecs_entity_t root = ecs_new(w);
    ecs_entity_t mid = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t leaf = ecs_new_w_pair(w, EcsChildOf, mid);
    ecs_set(w, root, Tint, {0, 0, 255, 128});
    ecs_set(w, mid, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, leaf, ActualTint)->b, 85);
    ecs_set(w, mid, Tint, {0, 0, 0, 0});
    test_int(ecs_get(w, leaf, ActualTint)->b, 255);
    test_int(ecs_get(w, leaf, ActualTint)->a, 128);
    test_int(ecs_get(w, root, ActualTint)->b, 255);
    test_int(ecs_get(w, root, ActualTint)->a, 128);
}

void Material_tint_removed_restores_the_ancestor_result(void)
{
    // materials/tint_compose
    ecs_entity_t root = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, root);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, root, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    ecs_remove(w, child, Tint);
    test_int(ecs_get(w, child, ActualTint)->b, 255);
    test_int(ecs_get(w, child, ActualTint)->a, 128);
    test_int(ecs_get(w, grandchild, ActualTint)->b, 255);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 128);
}

void Material_tint_composes_through_a_childof_chain(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
}

void Material_tint_composes_through_a_parent_chain(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_entity_t grandchild = ecs_new_w_parent(w, child, "grandchild");
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
}

void Material_tint_composes_with_childof_under_parent(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_parent(w, parent, "child");
    ecs_entity_t grandchild = ecs_new_w_pair(w, EcsChildOf, child);
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
}

void Material_tint_composes_with_parent_under_childof(void)
{
    // materials/tint_compose
    ecs_entity_t parent = ecs_new(w);
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, parent);
    ecs_entity_t grandchild = ecs_new_w_parent(w, child, "grandchild");
    ecs_set(w, parent, Tint, {0, 0, 255, 128});
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, grandchild, ActualTint)->b, 85);
    test_int(ecs_get(w, grandchild, ActualTint)->a, 192);
}

void Material_tint_composition_survives_a_reparent(void)
{
    // materials/tint_compose
    ecs_entity_t blue = ecs_new(w);
    ecs_set(w, blue, Tint, {0, 0, 255, 128});
    ecs_entity_t red = ecs_new(w);
    ecs_set(w, red, Tint, {255, 0, 0, 128});
    ecs_entity_t child = ecs_new_w_pair(w, EcsChildOf, blue);
    ecs_set(w, child, Tint, {0, 0, 0, 128});
    test_int(ecs_get(w, child, ActualTint)->b, 85);
    ecs_set(w, child, EcsParent, { red });
    test_int(ecs_get(w, child, ActualTint)->r, 85);
    test_int(ecs_get(w, child, ActualTint)->b, 0);
    test_int(ecs_get(w, child, ActualTint)->a, 192);
    ecs_add_pair(w, child, EcsChildOf, blue);
    test_int(ecs_get(w, child, ActualTint)->b, 85);
    test_int(ecs_get(w, child, ActualTint)->r, 0);
}
