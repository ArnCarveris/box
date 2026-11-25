/**
 * @file Movement.c
 * @brief Tests for Velocity and AngularVelocity moving entities.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Movement_linear(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Velocity, {2, -4, 8});
    test_assert(ecs_has(world, e, DynamicTransform));

    for (int i = 0; i < 10; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 11);
    test_flt(p->y, -18);
    test_flt(p->z, 43);
    test_flt(ecs_get(world, e, WorldTransform)->position[0], 11);
    test_flt(ecs_get(world, e, WorldTransform)->position[1], -18);

    ecs_fini(world);
}

void Movement_angular(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Rotation, {0.5f, 0, 0});
    ecs_set(world, e, AngularVelocity, {1, 2, -2});
    test_assert(ecs_has(world, e, DynamicTransform));

    for (int i = 0; i < 8; i ++) {
        ecs_progress(world, 0.25f);
    }

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 2.5f);
    test_flt(r->y, 4);
    test_flt(r->z, -4);

    ecs_fini(world);
}

void Movement_zero_velocity(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e, Velocity, {0, 0, 0});
    ecs_set(world, e, AngularVelocity, {0, 0, 0});

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 1.0f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);
    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);
    test_flt(r->z, 0.3f);

    ecs_fini(world);
}

void Movement_without_velocity(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_add(world, e, DynamicTransform);

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, e, Position)->x, 1);

    ecs_fini(world);
}

void Movement_child_follows_parent(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "mover {\n"
            "  Position: {0, 0, 0}\n"
            "  Velocity: {2, 0, 0}\n"
            "  child {\n"
            "    Position: {1, 0, 0}\n"
            "  }\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t parent = ecs_lookup(world, "mover");
    ecs_entity_t child = ecs_lookup(world, "mover.child");
    test_assert(parent != 0);
    test_assert(child != 0);

    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    test_flt(ecs_get(world, child, Position)->x, 1);
    WorldTransform t_buf;
    test_flt(ecs_get(world, parent, WorldTransform)->position[0], 4);
    test_flt(box_transform_world(world, child, &t_buf)->position[0], 5);

    ecs_fini(world);
}

void Movement_moving_child(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_new(world);
    ecs_set(world, parent, Position, {10, 0, 0});
    ecs_set(world, parent, Velocity, {1, 0, 0});

    ecs_entity_t child = ecs_new(world);
    ecs_add_pair(world, child, EcsChildOf, parent);
    ecs_set(world, child, Position, {0, 0, 0});
    ecs_set(world, child, Velocity, {0, 3, 0});

    for (int i = 0; i < 2; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, child, WorldTransform)->position[0], 12);
    test_flt(ecs_get(world, child, WorldTransform)->position[1], 6);

    ecs_fini(world);
}

void Movement_velocity_adds_position(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Velocity, {1, 0, 0});
    test_assert(ecs_has(world, e, Position));
    test_assert(!ecs_has(world, e, Rotation));
    test_assert(ecs_has(world, e, DynamicTransform));

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Movement_angular_velocity_adds_rotation(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AngularVelocity, {1, 0, 0});
    test_assert(ecs_has(world, e, Rotation));
    test_assert(!ecs_has(world, e, Velocity));
    test_assert(ecs_has(world, e, DynamicTransform));

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, 0);
    test_flt(r->z, 0);

    ecs_fini(world);
}

void Movement_velocity_keeps_position(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {1, 2, 3});
    ecs_set(world, e, Velocity, {0, 0, 0});

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 2);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void Movement_angular_velocity_keeps_rotation(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {4, 5, 6});
    ecs_set(world, e, Rotation, {0.1f, 0.2f, 0.3f});
    ecs_set(world, e, AngularVelocity, {0, 0, 0});

    const Rotation *r = ecs_get(world, e, Rotation);
    test_flt(r->x, 0.1f);
    test_flt(r->y, 0.2f);
    test_flt(r->z, 0.3f);
    test_flt(ecs_get(world, e, Position)->x, 4);

    ecs_fini(world);
}

void Movement_velocity_only_moves(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Velocity, {2, -1, 4});

    ecs_progress(world, 0.5f);

    const Position *p = ecs_get(world, e, Position);
    test_assert(p != NULL);
    test_flt(p->x, 1);
    test_flt(p->y, -0.5f);
    test_flt(p->z, 2);
    test_flt(ecs_get(world, e, WorldTransform)->position[0], 1);
    test_flt(ecs_get(world, e, WorldTransform)->position[2], 2);

    ecs_fini(world);
}

void Movement_angular_velocity_only_rotates(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AngularVelocity, {1, 2, -2});

    ecs_progress(world, 0.25f);

    const Rotation *r = ecs_get(world, e, Rotation);
    test_assert(r != NULL);
    test_flt(r->x, 0.25f);
    test_flt(r->y, 0.5f);
    test_flt(r->z, -0.5f);

    ecs_fini(world);
}

void Movement_velocity_from_script(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "mover {\n"
            "  Velocity: {2, 0, 0}\n"
            "}\n"
            "placed {\n"
            "  Position: {5, 6, 7}\n"
            "  Velocity: {0, 0, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t mover = ecs_lookup(world, "mover");
    ecs_entity_t placed = ecs_lookup(world, "placed");
    test_assert(mover != 0);
    test_assert(placed != 0);
    test_assert(ecs_has(world, mover, Position));
    test_assert(!ecs_has(world, mover, Rotation));
    test_flt(ecs_get(world, placed, Position)->x, 5);
    test_flt(ecs_get(world, placed, Position)->y, 6);
    test_flt(ecs_get(world, placed, Position)->z, 7);

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, mover, Position)->x, 2);
    test_flt(ecs_get(world, mover, WorldTransform)->position[0], 2);
    test_flt(ecs_get(world, placed, Position)->x, 5);

    ecs_fini(world);
}

void Movement_angular_velocity_from_script(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "spinner {\n"
            "  AngularVelocity: {0, 1, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t e = ecs_lookup(world, "spinner");
    test_assert(e != 0);
    test_assert(ecs_has(world, e, Rotation));

    ecs_progress(world, 0.5f);

    test_flt(ecs_get(world, e, Rotation)->y, 0.5f);

    ecs_fini(world);
}

void Movement_velocity_prefab_instance(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.movement\n"
            "prefab Mover {\n"
            "  Velocity: {1, 0, 0}\n"
            "  AngularVelocity: {0, 0, 2}\n"
            "}\n"
            "a : Mover {}\n"
            "b : Mover {\n"
            "  Position: {10, 0, 0}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t a = ecs_lookup(world, "a");
    ecs_entity_t b = ecs_lookup(world, "b");
    test_assert(a != 0);
    test_assert(b != 0);
    test_assert(ecs_owns(world, a, Position));
    test_assert(ecs_owns(world, a, Rotation));
    test_assert(ecs_owns(world, b, Position));

    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, a, Position)->x, 1);
    test_flt(ecs_get(world, a, Rotation)->z, 2);
    test_flt(ecs_get(world, b, Position)->x, 11);

    ecs_fini(world);
}

/** Create an entity with a Path3D.
 *
 * @param world The world
 * @param start The start point
 * @param segments The segments
 * @param count The number of segments
 * @param closed Whether the path is closed
 * @return The path entity
 */
static ecs_entity_t movement_path(
    ecs_world_t *world,
    vec3 start,
    const PathSegment3D *segments,
    int count,
    bool closed)
{
    Path3D path = { .closed = closed };
    glm_vec3_copy(start, path.start);
    for (int i = 0; i < count; i ++) {
        *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = segments[i];
    }
    ecs_entity_t e = ecs_new(world);
    ecs_set_ptr(world, e, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);
    return e;
}

static int32_t movement_errors;

/** Count the PathFollow errors.
 *
 * @param level The log level
 * @param file The source file
 * @param line The source line
 * @param msg The message
 */
static void MovementCaptureLog(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -3 && strstr(msg, "PathFollow")) {
        movement_errors ++;
    }
}

/** Create a line path from the origin.
 *
 * @param world The world
 * @param x End x
 * @param y End y
 * @param z End z
 * @return The path entity
 */
static ecs_entity_t movement_line(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    PathSegment3D s = { .kind = PathLine, .end = {x, y, z} };
    return movement_path(world, (vec3){0, 0, 0}, &s, 1, false);
}

void Movement_path_follow_line(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 2});
    test_assert(ecs_has(world, e, Position));
    test_assert(ecs_has(world, e, DynamicTransform));

    for (int i = 0; i < 4; i ++) {
        ecs_progress(world, 0.5f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 4);
    test_flt(p->y, 0);
    test_flt(p->z, 0);
    test_flt(ecs_get(world, e, PathFollow)->distance, 4);
    test_flt(ecs_get(world, e, WorldTransform)->position[0], 4);

    ecs_fini(world);
}

void Movement_path_follow_constant_speed(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    /* A Bezier whose parameter speed is far from uniform */
    PathSegment3D s = { .kind = PathCubicBezier, .end = {10, 0, 0},
        .control1 = {0, 8, 0}, .control2 = {1, 8, 0} };
    ecs_entity_t route = movement_path(world, (vec3){0, 0, 0}, &s, 1, false);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 1});

    vec3 previous = {0, 0, 0};
    for (int i = 0; i < 40; i ++) {
        ecs_progress(world, 0.25f);
        const Position *p = ecs_get(world, e, Position);
        vec3 point = {p->x, p->y, p->z};
        float step = glm_vec3_distance(previous, point);
        test_assert(fabsf(step - 0.25f) < 0.01f);
        glm_vec3_copy(point, previous);
    }

    ecs_fini(world);
}

void Movement_path_follow_once_stops(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 4, .mode = PathFollowOnce});

    for (int i = 0; i < 5; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, e, Position)->x, 10);
    test_flt(ecs_get(world, e, PathFollow)->distance, 10);
    test_flt(ecs_get(world, e, PathFollow)->speed, 4);

    ecs_fini(world);
}

void Movement_path_follow_loop_open(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 4, .mode = PathFollowLoop});

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, e, Position)->x, 2);
    test_flt(ecs_get(world, e, PathFollow)->distance, 2);

    ecs_fini(world);
}

void Movement_path_follow_loop_closed(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    /* A square of side 4 whose closing segment runs back from (0, 0, 4) to the start */
    PathSegment3D s[] = {
        { .kind = PathLine, .end = {4, 0, 0} },
        { .kind = PathLine, .end = {4, 0, 4} },
        { .kind = PathLine, .end = {0, 0, 4} }
    };
    ecs_entity_t route = movement_path(world, (vec3){0, 0, 0}, s, 3, true);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 5, .mode = PathFollowLoop});

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->z, 1);
    test_flt(ecs_get(world, e, PathFollow)->distance, 15);

    ecs_progress(world, 1.0f);

    p = ecs_get(world, e, Position);
    test_flt(p->x, 4);
    test_flt(p->z, 0);
    test_flt(ecs_get(world, e, PathFollow)->distance, 4);

    ecs_fini(world);
}

void Movement_path_follow_ping_pong(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 4, .mode = PathFollowPingPong});

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, e, Position)->x, 8);
    test_flt(ecs_get(world, e, PathFollow)->speed, -4);

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, e, Position)->x, 4);

    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, e, Position)->x, 4);
    test_flt(ecs_get(world, e, PathFollow)->speed, 4);

    ecs_fini(world);
}

void Movement_path_follow_negative_speed(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = -3, .distance = 10});

    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);

    test_flt(ecs_get(world, e, Position)->x, 4);

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    test_flt(ecs_get(world, e, Position)->x, 0);
    test_flt(ecs_get(world, e, PathFollow)->distance, 0);

    ecs_fini(world);
}

void Movement_path_follow_orient(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t along_x = ecs_new(world);
    ecs_set(world, along_x, PathFollow, {.path = movement_line(world, 10, 0, 0),
        .speed = 1, .orient = true});
    ecs_entity_t diagonal = ecs_new(world);
    ecs_set(world, diagonal, PathFollow, {.path = movement_line(world, 3, 4, 0),
        .speed = 1, .orient = true});
    ecs_entity_t up = ecs_new(world);
    ecs_set(world, up, PathFollow, {.path = movement_line(world, 0, 10, 0),
        .speed = 1, .orient = true});
    ecs_entity_t back = ecs_new(world);
    ecs_set(world, back, PathFollow, {.path = movement_line(world, 10, 0, 0),
        .speed = -1, .distance = 5, .orient = true});

    ecs_progress(world, 1.0f);

    /* The entity looks down -Z along the tangent */
    test_assert(ecs_has(world, along_x, Rotation));
    const Rotation *r = ecs_get(world, along_x, Rotation);
    test_flt(r->x, 0);
    test_flt(r->y, -GLM_PI_2f);
    test_flt(r->z, 0);

    ecs_progress(world, 1.0f);

    const WorldTransform *m = ecs_get(world, diagonal, WorldTransform);
    test_assert(fabsf(-m->basis[2][0] - 0.6f) < 1e-4f);
    test_assert(fabsf(-m->basis[2][1] - 0.8f) < 1e-4f);
    test_assert(fabsf(m->basis[2][2]) < 1e-4f);
    /* +Y stays up */
    test_assert(m->basis[1][1] > 0);

    /* Along Y the up of the entity is +X */
    m = ecs_get(world, up, WorldTransform);
    test_assert(fabsf(-m->basis[2][1] - 1) < 1e-4f);
    test_assert(fabsf(m->basis[1][0] - 1) < 1e-4f);

    /* Moving backwards faces the direction of travel */
    m = ecs_get(world, back, WorldTransform);
    test_assert(fabsf(-m->basis[2][0] + 1) < 1e-4f);

    ecs_fini(world);
}

void Movement_path_follow_seek(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 10, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .distance = 7});

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, e, Position)->x, 7);

    ecs_set(world, e, PathFollow, {.path = route, .speed = 1, .distance = 2});
    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, e, Position)->x, 3);

    ecs_fini(world);
}

void Movement_path_follow_prefab_path(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_script(world, {
        .code =
            "using box.transform\n"
            "using box.geometry\n"
            "using box.movement\n"
            "prefab Route {\n"
            "  Path3D: {start: {0, 0, 0}, segments: [{kind: Line, end: {0, 0, 10}}]}\n"
            "}\n"
            "route : Route {}\n"
            "drone {\n"
            "  PathFollow: {path: route, speed: 4, mode: PathFollowLoop, orient: true}\n"
            "}\n"
            "other {\n"
            "  PathFollow: {path: Route, speed: 2}\n"
            "}\n"
    });
    test_assert(s != 0);

    ecs_entity_t drone = ecs_lookup(world, "drone");
    ecs_entity_t other = ecs_lookup(world, "other");
    test_assert(drone != 0);
    test_assert(other != 0);
    test_assert(!ecs_owns(world, ecs_lookup(world, "route"), Path3D));

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    const Position *p = ecs_get(world, drone, Position);
    test_flt(p->x, 0);
    test_flt(p->z, 2);
    test_assert(ecs_has(world, drone, Rotation));
    test_flt(ecs_get(world, other, Position)->z, 6);

    /* Changing the Path3D of the prefab rebuilds the table of the instance that inherits it */
    PathSegment3D seg = { .kind = PathLine, .end = {20, 0, 0} };
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = seg;
    ecs_set_ptr(world, ecs_lookup(world, "Route"), Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);

    ecs_progress(world, 1.0f);
    p = ecs_get(world, drone, Position);
    test_flt(p->x, 6);
    test_flt(p->z, 0);

    ecs_fini(world);
}

void Movement_path_follow_path_changed(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t route = movement_line(world, 4, 0, 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, PathFollow, {.path = route, .speed = 3, .mode = PathFollowLoop});

    ecs_progress(world, 1.0f);
    test_flt(ecs_get(world, e, Position)->x, 3);

    /* A longer path along Z: the old table would wrap 6 to 2 along X */
    PathSegment3D s = { .kind = PathLine, .end = {0, 0, 10} };
    Path3D path = {0};
    *ecs_vec_append_t(NULL, &path.segments, PathSegment3D) = s;
    ecs_set_ptr(world, route, Path3D, &path);
    ecs_vec_fini_t(NULL, &path.segments, PathSegment3D);

    ecs_progress(world, 1.0f);

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 0);
    test_flt(p->z, 6);

    ecs_fini(world);
}

void Movement_path_follow_invalid_path(void) {
    // engine_core/movement
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t level = ecs_log_set_level(-1);
    bool colors = ecs_log_enable_colors(false);
    ecs_os_api_log_t real_log = ecs_os_api.log_;
    ecs_os_api.log_ = MovementCaptureLog;
    movement_errors = 0;

    ecs_entity_t no_path = ecs_new(world);
    ecs_entity_t empty = ecs_new(world);
    ecs_set_ptr(world, empty, Path3D, (&(Path3D){0}));
    PathSegment3D s = { .kind = PathLine, .end = {NAN, 0, 0} };
    ecs_entity_t nan = movement_path(world, (vec3){0, 0, 0}, &s, 1, false);

    ecs_entity_t paths[] = { 0, no_path, empty, nan };
    ecs_entity_t followers[4];
    for (int i = 0; i < 4; i ++) {
        followers[i] = ecs_new(world);
        ecs_set(world, followers[i], Position, {1, 2, 3});
        ecs_set(world, followers[i], PathFollow, {.path = paths[i], .speed = 1,
            .orient = true});
    }

    for (int i = 0; i < 3; i ++) {
        ecs_progress(world, 1.0f);
    }

    ecs_os_api.log_ = real_log;
    ecs_log_set_level(level);
    ecs_log_enable_colors(colors);

    /* One error per follower, not one per frame */
    test_int(movement_errors, 4);

    for (int i = 0; i < 4; i ++) {
        const Position *p = ecs_get(world, followers[i], Position);
        test_flt(p->x, 1);
        test_flt(p->y, 2);
        test_flt(p->z, 3);
        test_flt(ecs_get(world, followers[i], PathFollow)->distance, 0);
    }

    ecs_fini(world);
}
