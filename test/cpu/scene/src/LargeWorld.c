/**
 * @file LargeWorld.c
 * @brief Positions are f64: entities keep their precision far from the origin of the world,
 * world transforms compose in f64, nothing moves as the focus travels, and the focus, paths and
 * the spatial index work at any distance.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "spatial.h"

void LargeWorld_position_f64(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {1e7 + 0.125, 2.5, -1e7 - 0.0625});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0.001, 0, 0.002});
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, parent, Position);
    test_assert(p->x == 1e7 + 0.125);
    test_assert(p->z == -1e7 - 0.0625);
    const WorldTransform *t = ecs_get(world, child, WorldTransform);
    test_assert(t != NULL);
    test_assert(fabs(t->position[0] - (1e7 + 0.126)) < 1e-8);
    test_assert(fabs(t->position[1] - 2.5) < 1e-8);
    test_assert(fabs(t->position[2] - (-1e7 - 0.0605)) < 1e-8);

    ecs_fini(world);
}

void LargeWorld_rotated_parent_far(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {3e6, 0, 0});
    ecs_set(world, parent, Rotation, {0, (float)GLM_PI_2, 0});
    ecs_set(world, parent, Scale, {2, 2, 2});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, child, Position, {0.25, 0, 0});
    ecs_progress(world, 0);

    const WorldTransform *t = ecs_get(world, child, WorldTransform);
    test_assert(t != NULL);
    test_assert(fabs(t->position[0] - 3e6) < 1e-6);
    test_assert(fabs(t->position[2] + 0.5) < 1e-6);
    test_flt(box_transform_world_scale(t), 2.0f);

    mat4 m;
    box_transform_mat4(t, (double[3]){ 3e6, 0, 0 }, m);
    test_assert(fabsf(m[3][0]) < 1e-6f);
    test_assert(fabsf(m[3][2] + 0.5f) < 1e-6f);

    ecs_fini(world);
}

void LargeWorld_walk_1000_km(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t focus = ecs_new(world);
    ecs_set(world, focus, Position, {0, 0, 0});
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t landmark = ecs_new(world);
    ecs_set(world, landmark, Position, {0, 0, 5});
    for (int32_t i = 0; i < 1000; i ++) {
        const Position *p = ecs_get(world, focus, Position);
        ecs_set(world, focus, Position, {p->x + 1000.001, p->y, p->z});
        ecs_progress(world, 0);
    }

    const Position *p = ecs_get(world, focus, Position);
    test_assert(fabs(p->x - 1000001.0) < 1e-6);
    test_assert(fabs(ecs_get(world, focus, WorldTransform)->position[0] - 1000001.0) < 1e-6);
    const Position *l = ecs_get(world, landmark, Position);
    test_assert(l->x == 0 && l->y == 0 && l->z == 5);

    box_world_focus_t foci[1];
    test_int(box_world_focus(world, foci, 1), 1);
    test_assert(fabs(foci[0].position[0] - 1000001.0) < 1e-6);

    ecs_fini(world);
}

void LargeWorld_nothing_moves(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t tree = ecs_new(world);
    ecs_set(world, tree, Position, {12, 0, -7});
    ecs_set(world, tree, Box, {1, 4, 1});
    ecs_entity_t marker = ecs_new(world);
    ecs_set(world, marker, Position, {5, 5, 0});
    ecs_progress(world, 0);

    for (int32_t i = 0; i < 10; i ++) {
        ecs_set(world, camera, Position, {50000.0 * (i + 1), 2, -30000.0 * (i + 1)});
        ecs_progress(world, 0);
        test_assert(ecs_get(world, camera, Position)->x == 50000.0 * (i + 1));
    }

    const Position *t = ecs_get(world, tree, Position);
    test_assert(t->x == 12 && t->y == 0 && t->z == -7);
    test_assert(ecs_get(world, tree, WorldTransform)->position[0] == 12);
    const Position *m = ecs_get(world, marker, Position);
    test_assert(m->x == 5 && m->y == 5 && m->z == 0);

    ecs_fini(world);
}

void LargeWorld_focus_default_camera(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_world_focus_t foci[2];
    test_int(box_world_focus(world, foci, 2), 0);

    ecs_entity_t first = ecs_new(world);
    ecs_set(world, first, Position, {1e6 + 1, 2, 3});
    ecs_set(world, first, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t second = ecs_new(world);
    ecs_set(world, second, Position, {4, 5, 6});
    ecs_set(world, second, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view_b = ecs_new(world);
    ecs_set(world, view_b, View, {.camera = second, .order = 1});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = first, .order = 0});
    ecs_progress(world, 0);

    test_int(box_world_focus(world, foci, 2), 1);
    test_uint(foci[0].entity, first);
    test_uint(foci[0].scene, 0);
    test_assert(foci[0].position[0] == 1e6 + 1);
    test_assert(foci[0].position[1] == 2);
    test_assert(foci[0].position[2] == 3);

    ecs_fini(world);
}

void LargeWorld_focus_entities(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 0});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {5e6, 0, 0});
    ecs_add(world, a, WorldFocus);
    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {0, 5, 2e6});
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, Position, {0, 1, 0.5});
    ecs_add(world, b, WorldFocus);
    ecs_progress(world, 0);

    box_world_focus_t foci[4];
    test_int(box_world_focus(world, NULL, 0), 2);
    test_int(box_world_focus(world, foci, 4), 2);
    bool found_a = false, found_b = false;
    for (int32_t i = 0; i < 2; i ++) {
        if (foci[i].entity == a) {
            found_a = true;
            test_assert(foci[i].position[0] == 5e6);
        } else if (foci[i].entity == b) {
            found_b = true;
            test_assert(foci[i].position[1] == 6);
            test_assert(foci[i].position[2] == 2e6 + 0.5);
        }
    }

    test_assert(found_a && found_b);
    test_int(box_world_focus(world, foci, 1), 2);

    ecs_fini(world);
}

void LargeWorld_focus_in_scene(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t focus = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, focus, Position, {120000.5, 0, 0});
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    box_world_focus_t foci[1];
    test_int(box_world_focus(world, foci, 1), 1);
    test_uint(foci[0].entity, focus);
    test_uint(foci[0].scene, scene);
    test_assert(foci[0].position[0] == 120000.5);
    test_assert(ecs_get(world, focus, Position)->x == 120000.5);

    ecs_fini(world);
}

void LargeWorld_path_follow_far(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    Path3D line = { .start = {0, 0, 0} };
    *ecs_vec_append_t(NULL, &line.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathLine, .end = {100, 0, 0} };
    ecs_entity_t path = ecs_new(world);
    ecs_set_ptr(world, path, Path3D, &line);
    ecs_vec_fini_t(NULL, &line.segments, PathSegment3D);
    ecs_entity_t follower = ecs_new(world);
    ecs_set(world, follower, Position, {0, 0, 0});
    ecs_set(world, follower, PathFollow, {.path = path, .offset = {2e6 + 0.25, 0, -2e6}});
    ecs_progress(world, 0);
    test_assert(ecs_get(world, follower, Position)->x == 2e6 + 0.25);

    ecs_get_mut(world, follower, PathFollow)->speed = 10;
    ecs_progress(world, 1);
    const Position *p = ecs_get(world, follower, Position);
    test_assert(fabs(p->x - (2e6 + 10.25)) < 1e-4);
    test_assert(p->z == -2e6);

    ecs_fini(world);
}

void LargeWorld_spatial_far(void) {
    // scenes/large_world
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, Position, {1e6, 0, 1e6 + 10});
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t b = ecs_new(world);
    ecs_set(world, b, Position, {1e6, 0, 1e6 + 20});
    ecs_set(world, b, Box, {2, 2, 2});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, child, Position, {0, 0, 10});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_entity_t other = ecs_new(world);
    ecs_set(world, other, Position, {1e6 + 100, 0, 1e6 + 10});
    ecs_set(world, other, Box, {2, 2, 2});
    ecs_progress(world, 0);

    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    box_spatial_ray_t ray = { .origin = {1e6, 0, 1e6}, .dir = {0, 0, 1} };
    test_int(box_spatial_ray_all(world, &ray, &hits), 3);
    test_uint(ecs_vec_get_t(&hits, box_spatial_hit_t, 0)->entity, a);
    test_uint(ecs_vec_get_t(&hits, box_spatial_hit_t, 1)->entity, b);
    test_uint(ecs_vec_get_t(&hits, box_spatial_hit_t, 2)->entity, child);
    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);

    vec3 min, max;
    ecs_entity_t scene = 1;
    test_assert(box_spatial_bounds(world, other, &scene, min, max));
    test_assert(min[0] <= 1e6f + 99 && min[0] > 1e6f + 98.8f);
    test_assert(max[0] >= 1e6f + 101 && max[0] < 1e6f + 101.2f);

    ecs_fini(world);
}
