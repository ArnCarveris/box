/**
 * @file Survive.c
 * @brief Tests that engine state follows the entities a restore recreates.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

void Survive_render_slots_are_rebuilt(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxGeometry);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\nusing box.geometry\n\n"
            "a {\n  Position: {1, 0, 0}\n  Box: {1, 1, 1}\n}\n"
    }) != 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.static_updates.count, 1);
    test_flt(((box_static_update_t*)
        box_vec_at(&packet.static_updates, 0))->data.transform[3], 1);

    int32_t i;
    for (i = 0; i < (int32_t)packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_reset(&packet);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {5, 0, 0});
    box_render_extract(world, &packet);
    test_int(packet.static_updates.count, 1);
    box_packet_reset(&packet);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);
    box_render_extract(world, &packet);

    test_int(packet.static_updates.count, 1);
    box_static_update_t *update = box_vec_at(&packet.static_updates, 0);
    test_int(update->slot, 0);
    test_flt(update->data.transform[3], 1);
    test_int(packet.static_counts.count, 1);
    test_int(((box_static_count_t*)
        box_vec_at(&packet.static_counts, 0))->count, 1);

    for (i = 0; i < (int32_t)packet.mesh_create.count; i ++) {
        box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, i);
        free(cmd->vertices);
        free(cmd->indices);
    }

    box_packet_fini(&packet);
    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Survive_collider_is_rebuilt(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCollision);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\nusing box.geometry\n\n"
            "a {\n  Position: {0, 0, 0}\n  Box: {2, 2, 2}\n}\n"
    }) != 0);

    ecs_progress(world, 0);
    test_bool(box_collision_ray(world, vec3_new(0, 0, -10),
        vec3_new(0, 0, 1), 100, NULL), true);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "a"), Position, {20, 0, 0});
    ecs_progress(world, 0);
    test_bool(box_collision_ray(world, vec3_new(0, 0, -10),
        vec3_new(0, 0, 1), 100, NULL), false);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);
    ecs_progress(world, 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    test_assert(a != 0);
    const Collider *c = ecs_get(world, a, Collider);
    test_assert(c != NULL);
    test_int(c->kind, ColliderBox);
    test_flt(c->center.x, 0);

    box_ray_hit_t hit = {0};
    test_bool(box_collision_ray(world, vec3_new(0, 0, -10),
        vec3_new(0, 0, 1), 100, &hit), true);
    test_uint(hit.entity, a);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Survive_root_bounds_are_rebuilt(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCollision);
    ECS_IMPORT(world, BoxScript);
    ecs_singleton_set(world, ScriptSettings, { .enabled = false });

    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\nusing box.geometry\n\n"
            "template Tree {\n  trunk {\n    Position: {0, 1, 0}\n"
            "    Box: {1, 2, 1}\n  }\n}\n"
            "\ntree {\n  Position: {3, 0, 0}\n  Tree\n}\n"
    }) != 0);

    box_packet_t packet;
    box_packet_init(&packet);
    int32_t i, j;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_extract(world, &packet);
        for (j = 0; j < (int32_t)packet.mesh_create.count; j ++) {
            box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, j);
            free(cmd->vertices);
            free(cmd->indices);
        }

        box_packet_reset(&packet);
    }

    ecs_entity_t trunk = ecs_lookup(world, "tree.trunk");
    test_assert(trunk != 0);
    test_assert(ecs_has(world, ecs_lookup(world, "tree"), RootBounds));
    test_int(ecs_get(world, ecs_lookup(world, "tree"), RootBounds)->count, 1);
    test_bool(box_collision_ray(world, vec3_new(3, 1, -10),
        vec3_new(0, 0, 1), 100, NULL), true);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    ecs_set(world, ecs_lookup(world, "tree"), Position, {9, 0, 0});
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_extract(world, &packet);
        box_packet_reset(&packet);
    }

    test_bool(box_collision_ray(world, vec3_new(3, 1, -10),
        vec3_new(0, 0, 1), 100, NULL), false);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_extract(world, &packet);
        for (j = 0; j < (int32_t)packet.mesh_create.count; j ++) {
            box_mesh_cmd_t *cmd = box_vec_at(&packet.mesh_create, j);
            free(cmd->vertices);
            free(cmd->indices);
        }

        box_packet_reset(&packet);
    }

    ecs_entity_t tree = ecs_lookup(world, "tree");
    test_assert(tree != 0);
    test_assert(ecs_has(world, tree, RootBounds));
    test_int(ecs_get(world, tree, RootBounds)->count, 1);

    box_ray_hit_t hit = {0};
    test_bool(box_collision_ray(world, vec3_new(3, 1, -10),
        vec3_new(0, 0, 1), 100, &hit), true);
    test_uint(hit.entity, ecs_lookup(world, "tree.trunk"));

    box_packet_fini(&packet);
    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}

void Survive_controller_keeps_driving_the_entity(void)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxController);
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = "using box.transform\nusing box.controller\n\n"
            "cam {\n  Position: {0, 0, 0}\n  CameraController: {speed: 10}\n}\n"
    }) != 0);

    ecs_entity_t before = ecs_lookup(world, "cam");
    test_assert(before != 0);
    box_input_inject(world, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyW });

    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    float moved = ecs_get(world, before, Position)->z;
    test_assert(moved > 1.0f);

    char *folder = box_checkpoint_write(world, NULL, NULL);
    test_assert(folder != NULL);

    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(ecs_get(world, before, Position)->z > moved + 1.0f);

    char *restored = box_checkpoint_restore(world, NULL, 0, NULL);
    test_assert(restored != NULL);

    ecs_entity_t after = ecs_lookup(world, "cam");
    test_assert(after != 0);
    test_assert(after != before);
    test_flt(ecs_get(world, after, Position)->z, moved);

    for (i = 0; i < 5; i ++) {
        ecs_progress(world, 0.1f);
    }

    test_assert(ecs_get(world, after, Position)->z > moved + 1.0f);
    test_assert(ecs_get(world, after, CameraController) != NULL);

    ecs_os_free(restored);
    ecs_os_free(folder);
    ecs_fini(world);
}
