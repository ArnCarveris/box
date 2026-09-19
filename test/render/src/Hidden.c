/**
 * @file Hidden.c
 * @brief Tests for the Hidden switch: what leaves the frame and what comes back.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Hidden_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -10});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Hidden_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Hidden_an_entity_leaves_the_batch(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 1);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    test_int(packet.static_counts.count, 1);
    box_static_count_t *count = box_vec_at(&packet.static_counts, 0);
    test_int(count->count, 0);
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->hidden, true);
}

void Hidden_showing_it_again_takes_a_slot_back(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {1, 2, 3});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    box_static_update_t before = *(box_static_update_t*)box_vec_at(
        &packet.static_updates, 0);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);

    box_packet_reset(&packet);
    ecs_remove_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
    box_static_update_t *after = box_vec_at(&packet.static_updates, 0);
    test_int(after->slot, before.slot);
    test_int(after->mesh, before.mesh);
    test_assert(!memcmp(&after->data, &before.data, sizeof(box_instance_t)));
    const RenderInstance *inst = ecs_get(w, e, RenderInstance);
    test_bool(inst->hidden, false);
}

void Hidden_the_subtree_leaves_with_its_parent(void)
{
    // ore_engine/visibility
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Position, {0, 0, 0});
    ecs_set(w, parent, Box, {1, 1, 1});
    ecs_entity_t child = ecs_new(w);
    ecs_add_pair(w, child, EcsChildOf, parent);
    ecs_set(w, child, Position, {2, 0, 0});
    ecs_set(w, child, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 2);

    box_packet_reset(&packet);
    ecs_add_id(w, parent, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 0);
    box_static_count_t *count = box_vec_at(&packet.static_counts, 0);
    test_int(count->count, 0);
    const RenderInstance *inst = ecs_get(w, child, RenderInstance);
    test_bool(inst->hidden, true);

    box_packet_reset(&packet);
    ecs_remove_id(w, parent, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 2);
}

void Hidden_a_child_alone_leaves_its_parent_drawn(void)
{
    // ore_engine/visibility
    ecs_entity_t parent = ecs_new(w);
    ecs_set(w, parent, Position, {0, 0, 0});
    ecs_set(w, parent, Box, {1, 1, 1});
    ecs_entity_t child = ecs_new(w);
    ecs_add_pair(w, child, EcsChildOf, parent);
    ecs_set(w, child, Position, {2, 0, 0});
    ecs_set(w, child, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);

    box_packet_reset(&packet);
    ecs_add_id(w, child, Hidden);
    box_render_extract(w, &packet);
    box_static_count_t *count = box_vec_at(&packet.static_counts, 0);
    test_int(count->count, 1);
    test_bool(ecs_get(w, parent, RenderInstance)->hidden, false);
    test_bool(ecs_get(w, child, RenderInstance)->hidden, true);
}

void Hidden_a_dynamic_entity_leaves_the_frame(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_add_id(w, e, DynamicTransform);
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.dynamic_instances.count, 1);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.dynamic_instances.count, 0);

    box_packet_reset(&packet);
    ecs_remove_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.dynamic_instances.count, 1);
}

void Hidden_a_transparent_entity_leaves_the_sorted_list(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    ecs_set(w, e, Opacity, {0.5f});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.transparent.count, 1);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 0);

    box_packet_reset(&packet);
    ecs_remove_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.transparent.count, 1);
}

void Hidden_a_point_light_leaves_the_packet(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, PointLight, {.intensity = 5, .range = 20});
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 0);

    box_packet_reset(&packet);
    ecs_remove_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.lights.count, 1);
}

void Hidden_a_sun_leaves_the_view(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Rotation, {0.8f, 0, 0});
    ecs_set(w, e, DirectionalLight, {.intensity = 7});
    box_render_extract(w, &packet);
    test_flt(packet.view.sun_intensity, 7);

    box_packet_reset(&packet);
    ecs_add_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_flt(packet.view.sun_intensity, 3);

    box_packet_reset(&packet);
    ecs_remove_id(w, e, Hidden);
    box_render_extract(w, &packet);
    test_flt(packet.view.sun_intensity, 7);
}

void Hidden_an_instance_of_a_hidden_prefab_is_hidden(void)
{
    // ore_engine/visibility
    ecs_entity_t prefab = ecs_new(w);
    ecs_add_id(w, prefab, EcsPrefab);
    ecs_set(w, prefab, Box, {1, 1, 1});
    ecs_add_id(w, prefab, Hidden);

    ecs_entity_t e = ecs_new(w);
    ecs_add_pair(w, e, EcsIsA, prefab);
    ecs_set(w, e, Position, {0, 0, 0});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_int(packet.static_updates.count, 0);
    test_bool(ecs_get(w, e, RenderInstance)->hidden, true);

    box_packet_reset(&packet);
    ecs_remove_id(w, prefab, Hidden);
    box_render_extract(w, &packet);
    test_int(packet.static_updates.count, 1);
}

void Hidden_a_world_without_the_tag_resolves_nothing(void)
{
    // ore_engine/visibility
    ecs_entity_t e = ecs_new(w);
    ecs_set(w, e, Position, {0, 0, 0});
    ecs_set(w, e, Box, {1, 1, 1});
    box_render_extract(w, &packet);
    box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, 0);
    free(cmd->vertices);
    free(cmd->indices);
    test_bool(box_render_state(w)->hide_used, false);

    ecs_add_id(w, e, Hidden);
    test_bool(box_render_state(w)->hide_used, true);
}
