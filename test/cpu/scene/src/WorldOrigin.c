/**
 * @file WorldOrigin.c
 * @brief Floating world origin: shifts by whole steps when the focus passes the threshold,
 * what moves with a shift and what stays, the f64 origin and the spatial index across shifts.
 *
 * Most cases use a threshold of 100 and a step of 50, so a focus at x = 120 shifts the origin
 * by 100 (two steps) and lands at x = 20.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include "spatial.h"

/** Create a world with box and a WorldOrigin with a small threshold and step.
 *
 * @param threshold The threshold of the origin
 * @param step The step of the origin
 * @return The world
 */
static ecs_world_t* origin_world(
    float threshold,
    float step)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, WorldOrigin, {
        .threshold = threshold, .step = step, .enabled = true
    });

    return world;
}

/** Create a root entity with a position.
 *
 * @param world The world
 * @param x X position
 * @param y Y position
 * @param z Z position
 * @return The entity
 */
static ecs_entity_t origin_entity(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});

    return e;
}

/** Keep the last WorldOriginShift event, its scene plus 1000 to tell it from no event.
 *
 * @param it The iterator of the observer, its ctx the kept event
 */
static void OriginOnShift(
    ecs_iter_t *it)
{
    WorldOriginShift *last = it->ctx;
    *last = *(const WorldOriginShift*)it->param;
    last->scene += 1000;
}

void WorldOrigin_defaults(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(!ecs_singleton_get(world, WorldOrigin));
    ecs_singleton_add(world, WorldOrigin);
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o != NULL);
    test_assert(o->x == 0 && o->y == 0 && o->z == 0);
    test_flt(o->threshold, BOX_WORLD_ORIGIN_THRESHOLD);
    test_flt(o->step, BOX_WORLD_ORIGIN_STEP);
    test_bool(o->enabled, true);
    test_assert(ecs_singleton_get(world, WorldOriginStatus) != NULL);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 0);

    ecs_fini(world);
}

void WorldOrigin_no_origin_no_shift(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t focus = origin_entity(world, 100000, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 100000);
    test_assert(!ecs_singleton_get(world, WorldOrigin));

    ecs_fini(world);
}

void WorldOrigin_within_threshold(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 99, -99, 100);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    const Position *p = ecs_get(world, focus, Position);
    test_flt(p->x, 99);
    test_flt(p->y, -99);
    test_flt(p->z, 100);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 0);

    ecs_fini(world);
}

void WorldOrigin_shift_past_threshold(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 20);
    test_flt(ecs_get(world, focus, WorldTransform)->m[3][0], 20);
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o->x == 100);
    test_assert(o->y == 0);
    test_assert(o->z == 0);
    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    test_int(s->shifts, 1);
    test_assert(s->x == 100);

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 1);
    test_flt(ecs_get(world, focus, Position)->x, 20);

    ecs_fini(world);
}

void WorldOrigin_shift_whole_steps(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(1000, 256);
    ecs_entity_t focus = origin_entity(world, 1234.5f, 0, -3000.25f);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o->x == 1280);
    test_assert(o->z == -3072);
    test_assert(fmod(o->x, 256) == 0);
    test_assert(fmod(o->z, 256) == 0);
    const Position *p = ecs_get(world, focus, Position);
    test_flt(p->x, -45.5f);
    test_flt(p->z, 71.75f);

    ecs_fini(world);
}

void WorldOrigin_shift_per_axis(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 90, 180, -60);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o->x == 0);
    test_assert(o->y == 200);
    test_assert(o->z == 0);
    const Position *p = ecs_get(world, focus, Position);
    test_flt(p->x, 90);
    test_flt(p->y, -20);
    test_flt(p->z, -60);

    ecs_fini(world);
}

void WorldOrigin_disabled(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->enabled = false;
    ecs_singleton_modified(world, WorldOrigin);
    ecs_entity_t focus = origin_entity(world, 500, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 500);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 0);

    ecs_fini(world);
}

void WorldOrigin_roots_shift_children_dont(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t root = origin_entity(world, 130, 5, 0);
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Position, {1, 2, 3});
    ecs_entity_t grandchild = ecs_new_w_pair(world, EcsChildOf, child);
    ecs_set(world, grandchild, Position, {0, 0, 1});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, root, Position)->x, 30);
    test_flt(ecs_get(world, root, Position)->y, 5);
    test_flt(ecs_get(world, child, Position)->x, 1);
    test_flt(ecs_get(world, child, Position)->y, 2);
    test_flt(ecs_get(world, grandchild, Position)->z, 1);
    test_flt(ecs_get(world, child, WorldTransform)->m[3][0], 31);
    test_flt(ecs_get(world, grandchild, WorldTransform)->m[3][0], 31);
    test_flt(ecs_get(world, grandchild, WorldTransform)->m[3][2], 4);

    ecs_fini(world);
}

void WorldOrigin_scope_without_transform(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t member = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, member, Position, {140, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, member, Position)->x, 40);

    ecs_fini(world);
}

void WorldOrigin_dynamic_root(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 90, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_set(world, focus, Velocity, {20, 0, 0});
    ecs_entity_t mover = origin_entity(world, 0, 0, 0);
    ecs_set(world, mover, Velocity, {0, 0, 0});
    ecs_progress(world, 1);

    test_flt(ecs_get(world, focus, Position)->x, 10);
    test_flt(ecs_get(world, focus, WorldTransform)->m[3][0], 10);
    test_flt(ecs_get(world, mover, Position)->x, -100);
    test_flt(ecs_get(world, mover, WorldTransform)->m[3][0], -100);

    ecs_fini(world);
}

void WorldOrigin_camera_shifts(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t camera = origin_entity(world, 0, 0, 260);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t other = origin_entity(world, 10, 0, 240);
    ecs_progress(world, 0);

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o->z == 250);
    test_flt(ecs_get(world, camera, Position)->z, 10);
    test_flt(ecs_get(world, other, Position)->z, -10);

    ecs_fini(world);
}

void WorldOrigin_focus_default_camera(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_world_focus_t foci[2];
    test_int(box_world_focus(world, foci, 2), 0);

    ecs_entity_t first = origin_entity(world, 1, 2, 3);
    ecs_set(world, first, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t second = origin_entity(world, 4, 5, 6);
    ecs_set(world, second, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view_b = ecs_new(world);
    ecs_set(world, view_b, View, {.camera = second, .order = 1});
    ecs_entity_t view_a = ecs_new(world);
    ecs_set(world, view_a, View, {.camera = first, .order = 0});
    ecs_progress(world, 0);

    test_int(box_world_focus(world, foci, 2), 1);
    test_uint(foci[0].entity, first);
    test_uint(foci[0].scene, 0);
    test_flt(foci[0].position[0], 1);
    test_flt(foci[0].position[1], 2);
    test_flt(foci[0].position[2], 3);

    ecs_fini(world);
}

void WorldOrigin_focus_entities(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = origin_entity(world, 0, 0, 0);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera});
    ecs_entity_t a = origin_entity(world, 10, 0, 0);
    ecs_add(world, a, WorldFocus);
    ecs_entity_t parent = origin_entity(world, 0, 5, 0);
    ecs_entity_t b = ecs_new_w_pair(world, EcsChildOf, parent);
    ecs_set(world, b, Position, {0, 1, 0});
    ecs_add(world, b, WorldFocus);
    ecs_progress(world, 0);

    box_world_focus_t foci[4];
    test_int(box_world_focus(world, NULL, 0), 2);
    test_int(box_world_focus(world, foci, 4), 2);
    bool found_a = false, found_b = false;
    for (int32_t i = 0; i < 2; i ++) {
        if (foci[i].entity == a) {
            found_a = true;
            test_flt(foci[i].position[0], 10);
        } else if (foci[i].entity == b) {
            found_b = true;
            test_flt(foci[i].position[1], 6);
        }
    }

    test_assert(found_a && found_b);
    test_int(box_world_focus(world, foci, 1), 2);

    ecs_fini(world);
}

void WorldOrigin_fixed_stays(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t fixed = origin_entity(world, 7, 0, 0);
    ecs_add(world, fixed, WorldOriginFixed);
    ecs_entity_t group = ecs_new(world);
    ecs_add(world, group, WorldOriginFixed);
    ecs_entity_t member = ecs_new_w_pair(world, EcsChildOf, group);
    ecs_set(world, member, Position, {8, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 20);
    test_flt(ecs_get(world, fixed, Position)->x, 7);
    test_flt(ecs_get(world, member, Position)->x, 8);

    ecs_fini(world);
}

void WorldOrigin_ui_stays(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t ui = ecs_new(world);
    ecs_set(world, ui, Scene, {0});
    ecs_entity_t ui_camera = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, ui_camera, Position, {0, 0, 100});
    ecs_entity_t panel = ecs_new_w_pair(world, EcsChildOf, ui);
    ecs_set(world, panel, Position, {300, 0, 0});
    ecs_entity_t layout = origin_entity(world, 400, 0, 0);
    ecs_set(world, layout, Layout, { .direction = DirectionRow });
    ecs_entity_t item = origin_entity(world, 500, 0, 0);
    ecs_set(world, item, LayoutItem, { .h = AlignLeft });
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 64, .height = 64});
    ecs_entity_t on_surface = ecs_new_w_pair(world, EcsChildOf, surface);
    ecs_set(world, on_surface, Position, {600, 0, 0});
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 20);
    test_flt(ecs_get(world, ui_camera, Position)->z, 100);
    test_flt(ecs_get(world, panel, Position)->x, 300);
    test_flt(ecs_get(world, layout, Position)->x, 400);
    test_flt(ecs_get(world, item, Position)->x, 500);
    test_flt(ecs_get(world, on_surface, Position)->x, 600);

    ecs_fini(world);
}

void WorldOrigin_focus_in_scene(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {0});
    ecs_entity_t focus = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, focus, Position, {120, 0, 0});
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t inside = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, inside, Position, {150, 0, 0});
    ecs_entity_t outside = origin_entity(world, 150, 0, 0);
    WorldOriginShift last = {0};
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldOrigin) }},
        .events = { ecs_id(WorldOriginShift) },
        .callback = OriginOnShift,
        .ctx = &last
    });
    ecs_progress(world, 0);

    test_flt(ecs_get(world, focus, Position)->x, 20);
    test_flt(ecs_get(world, inside, Position)->x, 50);
    test_flt(ecs_get(world, outside, Position)->x, 150);
    test_assert(last.x == 100);
    test_uint(last.scene, scene + 1000);

    ecs_fini(world);
}

void WorldOrigin_event(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, -130, 0, 260);
    ecs_add(world, focus, WorldFocus);
    WorldOriginShift last = {0};
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(WorldOrigin) }},
        .events = { ecs_id(WorldOriginShift) },
        .callback = OriginOnShift,
        .ctx = &last
    });
    ecs_progress(world, 0);

    test_assert(last.x == -150);
    test_assert(last.y == 0);
    test_assert(last.z == 250);
    test_uint(last.scene, 1000);

    ecs_fini(world);
}

void WorldOrigin_walk_1000_km(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_add(world, WorldOrigin);
    ecs_entity_t focus = origin_entity(world, 0, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t landmark = origin_entity(world, 0, 0, 5);
    for (int32_t i = 0; i < 1000; i ++) {
        const Position *p = ecs_get(world, focus, Position);
        ecs_set(world, focus, Position, {p->x + 1000.0f, p->y, p->z});
        ecs_progress(world, 0);
        test_assert(fabsf(ecs_get(world, focus, Position)->x) <= 2048.0f);
    }

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    const Position *p = ecs_get(world, focus, Position);
    test_assert(fmod(o->x, 256.0) == 0);
    test_assert(o->x + (double)p->x == 1000000.0);
    double absolute[3];
    box_world_to_absolute(world, (vec3){p->x, p->y, p->z}, absolute);
    test_assert(absolute[0] == 1000000.0);
    const Position *l = ecs_get(world, landmark, Position);
    box_world_to_absolute(world, (vec3){l->x, l->y, l->z}, absolute);
    test_assert(absolute[0] == 0.0);
    test_assert(absolute[2] == 5.0);
    test_assert(ecs_singleton_get(world, WorldOriginStatus)->shifts > 100);

    ecs_fini(world);
}

void WorldOrigin_to_local(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    vec3 local;
    double absolute[3] = { 1000010.5, -2, 3 };
    box_world_to_local(world, absolute, local);
    test_flt(local[0], 1000010.5f);

    ecs_singleton_set(world, WorldOrigin, {.x = 1000000, .y = -256, .z = 0});
    box_world_to_local(world, absolute, local);
    test_flt(local[0], 10.5f);
    test_flt(local[1], 254);
    test_flt(local[2], 3);
    double back[3];
    box_world_to_absolute(world, local, back);
    test_assert(back[0] == 1000010.5);
    test_assert(back[1] == -2);
    test_assert(back[2] == 3);

    ecs_fini(world);
}

void WorldOrigin_script_functions(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, WorldOrigin, {.x = 1000000, .y = 512, .z = -256});
    test_int(ecs_script_run(world, "origin",
        "probe {\n"
        "  box.world.WorldOriginShift: {\n"
        "    x: box.world.absolute_x(10.5),\n"
        "    y: box.world.local_y(520),\n"
        "    z: box.world.absolute_z(1)\n"
        "  }\n"
        "}\n"
        "back {\n"
        "  box.world.WorldOriginShift: {\n"
        "    x: box.world.local_x(1000010.5),\n"
        "    y: box.world.absolute_y(8),\n"
        "    z: box.world.local_z(-255)\n"
        "  }\n"
        "}\n", NULL), 0);

    const WorldOriginShift *probe = ecs_get(world, ecs_lookup(world, "probe"), WorldOriginShift);
    test_assert(probe != NULL);
    test_assert(probe->x == 1000010.5);
    test_assert(probe->y == 8);
    test_assert(probe->z == -255);
    const WorldOriginShift *back = ecs_get(world, ecs_lookup(world, "back"), WorldOriginShift);
    test_assert(back != NULL);
    test_assert(back->x == 10.5);
    test_assert(back->y == 520);
    test_assert(back->z == 1);

    ecs_fini(world);
}

void WorldOrigin_manual_shift(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = origin_entity(world, 300, 0, 0);
    ecs_progress(world, 0);

    box_world_origin_shift(world, 0, (double[3]){ 256, 0, 0 });
    test_flt(ecs_get(world, e, Position)->x, 44);
    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    test_assert(o != NULL);
    test_assert(o->x == 256);
    test_bool(o->enabled, false);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 1);

    ecs_progress(world, 0);
    test_flt(ecs_get(world, e, Position)->x, 44);
    test_flt(ecs_get(world, e, WorldTransform)->m[3][0], 44);

    ecs_fini(world);
}

void WorldOrigin_path_follow(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    Path3D line = { .start = {200, 0, 0} };
    *ecs_vec_append_t(NULL, &line.segments, PathSegment3D) = (PathSegment3D){
        .kind = PathLine, .end = {300, 0, 0} };
    ecs_entity_t path = ecs_new(world);
    ecs_set_ptr(world, path, Path3D, &line);
    ecs_vec_fini_t(NULL, &line.segments, PathSegment3D);
    ecs_entity_t follower = origin_entity(world, 0, 0, 0);
    ecs_set(world, follower, PathFollow, {.path = path});
    ecs_add(world, follower, WorldFocus);
    ecs_progress(world, 0);

    test_flt(ecs_get(world, follower, Position)->x, 0);
    test_flt(ecs_get(world, follower, PathFollow)->offset[0], -200);

    ecs_get_mut(world, follower, PathFollow)->speed = 10;

    ecs_progress(world, 1);
    test_assert(fabsf(ecs_get(world, follower, Position)->x - 10) < 1e-3f);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 200);

    ecs_fini(world);
}

void WorldOrigin_ground_heights(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t player = origin_entity(world, 0, 160, 0);
    ecs_set(world, player, FirstPersonController, {.ground = 150, .eye = 1.7f});
    ecs_add(world, player, WorldFocus);
    ecs_entity_t emitter = origin_entity(world, 0, 150, 0);
    ecs_set(world, emitter, ParticleCollision, {.ground = true, .height = 150});
    ecs_progress(world, 0);

    test_assert(ecs_singleton_get(world, WorldOrigin)->y == 150);
    test_flt(ecs_get(world, player, FirstPersonController)->ground, 0);
    test_flt(ecs_get(world, emitter, ParticleCollision)->height, 0);

    ecs_fini(world);
}

void WorldOrigin_follow_emitter_stays(void) {
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t player = origin_entity(world, 120, 0, 0);
    ecs_add(world, player, WorldFocus);
    ecs_entity_t emitter = origin_entity(world, 0, 3, 0);
    ecs_set(world, emitter, ParticleEmitter, {.follow = player});
    ecs_entity_t fixed = origin_entity(world, 0, 3, 0);
    ecs_progress(world, 0);

    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 100);
    test_flt(ecs_get(world, player, Position)->x, 20);
    test_flt(ecs_get(world, emitter, Position)->x, 0);
    test_flt(ecs_get(world, emitter, Position)->y, 3);
    test_flt(ecs_get(world, fixed, Position)->x, -100);

    ecs_fini(world);
}

/** Collect the entities a ray along +Z at x and y hits, nearest first.
 *
 * @param world The world
 * @param x X of the ray
 * @param y Y of the ray
 * @param z Z the ray starts at
 * @param out Receives the hits
 * @return Number of hits
 */
static int32_t origin_ray_hits(
    const ecs_world_t *world,
    float x,
    float y,
    float z,
    ecs_entity_t *out)
{
    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, box_spatial_hit_t, 0);
    box_spatial_ray_t ray = { .origin = {x, y, z}, .dir = {0, 0, 1} };
    int32_t count = box_spatial_ray_all(world, &ray, &hits);
    for (int32_t i = 0; i < count; i ++) {
        out[i] = ecs_vec_get_t(&hits, box_spatial_hit_t, i)->entity;
    }

    ecs_vec_fini_t(NULL, &hits, box_spatial_hit_t);

    return count;
}

void WorldOrigin_spatial_same_hits(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(1000, 256);
    ecs_entity_t focus = origin_entity(world, 900, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t a = origin_entity(world, 1200, 0, 10);
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t b = origin_entity(world, 1200, 0, 20);
    ecs_set(world, b, Box, {2, 2, 2});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, b);
    ecs_set(world, child, Position, {0, 0, 10});
    ecs_set(world, child, Box, {1, 1, 1});
    ecs_entity_t mover = origin_entity(world, 1200, 0, 40);
    ecs_set(world, mover, Box, {2, 2, 2});
    ecs_set(world, mover, Velocity, {0, 0, 0});
    ecs_entity_t other = origin_entity(world, 1300, 0, 10);
    ecs_set(world, other, Box, {2, 2, 2});
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 0);

    ecs_entity_t before[8], after[8];
    test_int(origin_ray_hits(world, 1200, 0, 0, before), 4);
    test_uint(before[0], a);
    test_uint(before[1], b);
    test_uint(before[2], child);
    test_uint(before[3], mover);
    vec3 min, max;
    ecs_entity_t scene = 1;
    test_assert(box_spatial_bounds(world, other, &scene, min, max));
    test_flt(min[0], 1299);

    ecs_set(world, focus, Position, {1100, 0, 0});
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 1);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 1024);

    test_int(origin_ray_hits(world, 176, 0, 0, after), 4);
    for (int32_t i = 0; i < 4; i ++) {
        test_uint(after[i], before[i]);
    }

    test_int(origin_ray_hits(world, 1200, 0, 0, after), 0);
    test_assert(box_spatial_bounds(world, other, &scene, min, max));
    test_flt(min[0], 275);
    test_flt(max[0], 277);

    ecs_progress(world, 0);
    test_int(origin_ray_hits(world, 176, 0, 0, after), 4);

    ecs_fini(world);
}

void WorldOrigin_spatial_before_transforms(void) {
    // scenes/world_origin
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t a = origin_entity(world, 3000, 0, 10);
    ecs_set(world, a, Box, {2, 2, 2});
    ecs_entity_t fixed = origin_entity(world, 2744, 0, 20);
    ecs_set(world, fixed, Box, {2, 2, 2});
    ecs_add(world, fixed, WorldOriginFixed);
    ecs_progress(world, 0);

    box_world_origin_shift(world, 0, (double[3]){ 256, 0, 0 });
    ecs_entity_t hits[4];
    test_int(origin_ray_hits(world, 2744, 0, 0, hits), 2);
    test_uint(hits[0], a);
    test_uint(hits[1], fixed);

    ecs_progress(world, 0);
    test_int(origin_ray_hits(world, 2744, 0, 0, hits), 2);
    test_uint(hits[0], a);
    test_uint(hits[1], fixed);
    test_int(origin_ray_hits(world, 3000, 0, 0, hits), 0);

    ecs_fini(world);
}

void WorldOrigin_status_stats(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    for (int32_t i = 0; i < 9; i ++) {
        origin_entity(world, (float)i, 0, 0);
    }

    ecs_progress(world, 0);

    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    test_int(s->shifts, 1);
    test_int(s->reason, 'T');
    test_int(s->written, 10);
    test_assert(s->ms >= 0);

    box_world_origin_shift(world, 0, (double[3]){0, 50, 0});
    s = ecs_singleton_get(world, WorldOriginStatus);
    test_int(s->shifts, 2);
    test_int(s->reason, 'M');

    ecs_fini(world);
}

void WorldOrigin_every_frame(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_singleton_get_mut(world, WorldOrigin)->every_frame = true;
    ecs_entity_t focus = origin_entity(world, 30, 0, 0);
    ecs_add(world, focus, WorldFocus);

    ecs_progress(world, 0);
    test_flt(ecs_get(world, focus, Position)->x, -20);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 50);
    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    test_int(s->shifts, 1);
    test_int(s->reason, 'O');

    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 1);

    ecs_set(world, focus, Position, {160, 0, 0});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, focus, Position)->x, 10);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 200);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->reason, 'T');

    ecs_fini(world);
}

void WorldOrigin_from_zero(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t focus = origin_entity(world, 120, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 100);

    ecs_singleton_get_mut(world, WorldOrigin)->from_zero = true;
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 0);
    test_flt(ecs_get(world, focus, Position)->x, 120);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->reason, 'Z');

    ecs_set(world, focus, Position, {5000, 0, 0});
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 0);
    test_flt(ecs_get(world, focus, Position)->x, 5000);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 2);

    ecs_fini(world);
}

void WorldOrigin_cell_placed(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    box_world_origin_shift(world, 0, (double[3]){4096000.0 - 10.0, 0, 0});

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, WorldCell, {1000, 0, -1});
    test_assert(ecs_has(world, root, DynamicTransform));
    const Position *p = ecs_get(world, root, Position);
    test_assert(p != NULL);
    test_flt(p->x, 10);
    test_flt(p->y, 0);
    test_flt(p->z, -4096);

    ecs_fini(world);
}

void WorldOrigin_cell_shift_exact(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100, 50);
    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, WorldCell, {250000, 0, 0});
    ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
    ecs_set(world, child, Position, {0.25f, 1, 0});
    ecs_progress(world, 0);

    for (int32_t i = 0; i < 1000; i ++) {
        box_world_origin_shift(world, 0, (double[3]){1024.0 + 0.125, 0, 0});
    }

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    double expect = 250000.0 * BOX_WORLD_CELL_SIZE - o->x;
    test_assert(ecs_get(world, root, Position)->x == (float)expect);
    test_flt(ecs_get(world, child, Position)->x, 0.25f);

    ecs_fini(world);
}

/** Time a shift of a root with descendants and the frame after it.
 *
 * @param count Descendants of the root
 * @param cell Whether the root is a WorldCell root, else a static root
 * @param written Receives the components the shift wrote
 * @param shift_ms Receives the milliseconds of the shift
 * @param frame_ms Receives the milliseconds of the frame after it
 */
static void origin_cell_cost(
    int32_t count,
    bool cell,
    int32_t *written,
    float *shift_ms,
    float *frame_ms)
{
    ecs_world_t *world = origin_world(100000, 4096);
    ecs_entity_t focus = origin_entity(world, 0, 0, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_entity_t root = ecs_new(world);
    if (cell) {
        ecs_set(world, root, WorldCell, {1, 0, 0});
    } else {
        ecs_set(world, root, Position, {4096, 0, 0});
    }

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t child = ecs_new_w_pair(world, EcsChildOf, root);
        ecs_set(world, child, Position, {(float)(i % 100), 0, (float)(i / 100)});
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    box_world_origin_shift(world, 0, (double[3]){4096, 0, 0});
    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    *written = s->written;
    *shift_ms = s->ms;

    ecs_time_t t = {0};
    ecs_time_measure(&t);
    ecs_progress(world, 0);
    *frame_ms = (float)(ecs_time_measure(&t) * 1000.0);
    test_flt(ecs_get(world, root, Position)->x, 0);

    ecs_fini(world);
}

void WorldOrigin_cell_cost(void) {
    // scenes/world_origin
    int32_t written[4];
    float shift_ms[4];
    float frame_ms[4];
    origin_cell_cost(1000, true, &written[0], &shift_ms[0], &frame_ms[0]);
    origin_cell_cost(100000, true, &written[1], &shift_ms[1], &frame_ms[1]);
    origin_cell_cost(1000, false, &written[2], &shift_ms[2], &frame_ms[2]);
    origin_cell_cost(100000, false, &written[3], &shift_ms[3], &frame_ms[3]);

    test_int(written[0], 2);
    test_int(written[1], 2);
    test_int(written[2], 2);
    test_int(written[3], 2);

    printf("WorldCell shift: 1k %.3f ms + frame %.3f ms, 100k %.3f ms + frame %.3f ms\n",
        shift_ms[0], frame_ms[0], shift_ms[1], frame_ms[1]);
    printf("Static root shift: 1k %.3f ms + frame %.3f ms, 100k %.3f ms + frame %.3f ms\n",
        shift_ms[2], frame_ms[2], shift_ms[3], frame_ms[3]);
}

/** Create a world whose origin turns its frame to a body of radius 1000 at the absolute origin,
 * with the focus at absolute X 1100.
 *
 * @param focus Receives the focus
 * @param body Receives the body
 * @return The world
 */
static ecs_world_t* origin_frame_world(
    ecs_entity_t *focus,
    ecs_entity_t *body)
{
    ecs_world_t *world = origin_world(100000, 50);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->rotate = true;
    ecs_singleton_modified(world, WorldOrigin);
    *body = origin_entity(world, 0, 0, 0);
    ecs_set(world, *body, WorldBody, {1000});
    *focus = origin_entity(world, 1100, 0, 0);
    ecs_add(world, *focus, WorldFocus);

    return world;
}

void WorldOrigin_frame_rotates_to_body(void) {
    // scenes/world_origin
    ecs_entity_t focus, body;
    ecs_world_t *world = origin_frame_world(&focus, &body);
    ecs_progress(world, 0);

    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    test_int(s->reason, 'R');
    test_assert(s->body == body);
    test_assert(fabs(s->altitude - 100.0) < 1e-9);
    const Position *f = ecs_get(world, focus, Position);
    test_assert(fabsf(f->x) < 1e-3f && fabsf(f->y) < 1e-3f && fabsf(f->z) < 1e-3f);
    const Position *b = ecs_get(world, body, Position);
    test_assert(fabsf(b->x) < 1e-3f);
    test_assert(fabsf(b->y + 1100.0f) < 1e-3f);
    test_assert(fabsf(b->z) < 1e-3f);

    double absolute[3];
    box_world_to_absolute(world, (vec3){0, 10, 0}, absolute);
    test_assert(fabs(absolute[0] - 1110.0) < 1e-9);
    test_assert(fabs(absolute[1]) < 1e-9);

    int32_t shifts = ecs_singleton_get(world, WorldOriginStatus)->shifts;
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, shifts);

    ecs_fini(world);
}

void WorldOrigin_frame_scales_with_altitude(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100000, 50);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->scale = true;
    o->scale_base = 64;
    ecs_singleton_modified(world, WorldOrigin);
    ecs_entity_t body = origin_entity(world, 0, 0, 0);
    ecs_set(world, body, WorldBody, {10});
    ecs_entity_t focus = origin_entity(world, 0, 1034, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    o = ecs_singleton_get_mut(world, WorldOrigin);
    test_assert(o->meters_per_unit == 16.0);
    test_assert(o->y == 1034.0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->reason, 'S');
    test_flt(ecs_get(world, body, Position)->y, -1034.0f / 16.0f);
    test_flt(ecs_get(world, body, Scale)->x, 1.0f / 16.0f);
    test_flt(ecs_get(world, focus, Position)->y, 0);

    double absolute[3];
    box_world_to_absolute(world, (vec3){0, 10, 0}, absolute);
    test_assert(absolute[1] == 1034.0 + 160.0);

    o->scale = false;
    ecs_singleton_modified(world, WorldOrigin);
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, WorldOrigin)->meters_per_unit == 1.0);
    test_flt(ecs_get(world, body, Scale)->x, 1.0f);
    test_flt(ecs_get(world, body, Position)->y, -1034.0f);

    ecs_fini(world);
}

void WorldOrigin_frame_rotate_off_restores(void) {
    // scenes/world_origin
    ecs_entity_t focus, body;
    ecs_world_t *world = origin_frame_world(&focus, &body);
    ecs_entity_t probe = origin_entity(world, 1100, 0, 5);
    ecs_set(world, probe, Rotation, {GLM_PI_2f, 0.7f, 0});
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->reason, 'R');
    test_assert(fabsf(ecs_get(world, probe, Position)->z - 5.0f) < 1e-3f);

    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->rotate = false;
    ecs_singleton_modified(world, WorldOrigin);
    ecs_progress(world, 0);

    o = ecs_singleton_get_mut(world, WorldOrigin);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->reason, 'R');
    test_assert(fabs(o->q[0]) < 1e-12 && fabs(o->q[1]) < 1e-12 && fabs(o->q[2]) < 1e-12);
    test_assert(fabsf(ecs_get(world, body, Position)->x + 1100.0f) < 1e-3f);

    const WorldTransform *after = ecs_get(world, probe, WorldTransform);
    mat4 identity;
    glm_euler_yxz((vec3){GLM_PI_2f, 0.7f, 0}, identity);
    for (int32_t c = 0; c < 3; c ++) {
        for (int32_t r = 0; r < 3; r ++) {
            test_assert(fabsf(after->m[c][r] - identity[c][r]) < 1e-5f);
        }
    }

    test_assert(fabsf(after->m[3][0]) < 1e-3f);
    test_assert(fabsf(after->m[3][2] - 5.0f) < 1e-3f);

    ecs_fini(world);
}

void WorldOrigin_frame_altitude_precise(void) {
    // scenes/world_origin
    ecs_world_t *world = origin_world(100000, 50);
    WorldOrigin *o = ecs_singleton_get_mut(world, WorldOrigin);
    o->rotate = true;
    ecs_singleton_modified(world, WorldOrigin);
    box_world_origin_shift(world, 0, (double[3]){0, 1.7e6, 0});

    ecs_entity_t moon = ecs_new(world);
    ecs_set(world, moon, WorldCell, {0, 0, 0});
    ecs_set(world, moon, WorldBody, {1.7e6});
    ecs_entity_t focus = origin_entity(world, 0, 1.7f, 0);
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    const WorldOriginStatus *s = ecs_singleton_get(world, WorldOriginStatus);
    test_assert(s->body == moon);
    test_assert(fabs(s->altitude - (double)1.7f) < 1e-9);
    test_int(s->shifts, 1);

    ecs_fini(world);
}
