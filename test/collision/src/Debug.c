/**
 * @file Debug.c
 * @brief Tests for the collider and root bounds wireframes the extract emits.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

void Debug_off_emits_no_lines(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 0);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_sphere_draws_three_circles(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugColliders
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 144);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_box_draws_twelve_edges(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugColliders
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 24);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_capsule_draws_two_circles_and_four_lines(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugColliders
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, CapsuleCollider, {{0, 0, 0}, 0.5f, 2});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 104);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_root_bounds_draw_a_box(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugBounds
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, child, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 24);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_all_draws_colliders_and_bounds(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugAll
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t prefab = ecs_new_w_id(world, EcsPrefab);
    ecs_entity_t inst = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, inst);
    ecs_set(world, child, AABB, {{-1, -1, -1}, {1, 1, 1}});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 48);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_collider_outside_the_view_is_skipped(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugColliders
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, -100});
    ecs_set(world, e, SphereCollider, {{0, 0, 0}, 1});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 0);
    box_packet_fini(&packet);

    ecs_fini(world);
}

void Debug_lines_are_cleared_between_packets(void)
{
    // collision/debug_draw
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, CollisionSettings, {
        .margin = 0.25f, .dynamic = true, .enabled = true,
        .debug = DebugColliders
    });

    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, {0, 0, -10});
    ecs_set(world, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = cam, .enabled = true});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, BoxCollider, {{0, 0, 0}, {1, 1, 1}});
    ecs_progress(world, 0);

    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 24);
    box_packet_reset(&packet);
    packet.width = 640;
    packet.height = 480;
    box_render_extract(world, &packet);
    test_int(packet.lines.count, 24);
    box_packet_fini(&packet);

    ecs_fini(world);
}
