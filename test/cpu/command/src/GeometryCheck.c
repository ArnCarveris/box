/**
 * @file GeometryCheck.c
 * @brief Tests for geometry.check, the lint that lists the pairs of 3D shapes with faces that
 * z-fight, and for the geometry command.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>

void GeometryCheck_coplanar_faces(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.5f, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar a and b: area 2.0000, gap 0.00000, facing (0.00, -1.00, 0.00) at "
        "(0.00, 0.50, 0.00)\n"
        "1 coplanar pair in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_single_face(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0.25f, 0.75f, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar a and b: area 1.0000, gap 0.00000, facing (0.00, 1.00, 0.00) at "
        "(0.25, 1.00, 0.00)\n"
        "1 coplanar pair in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_opposite_faces(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 1.5f, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_tolerance(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.76f, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 2 shapes, 24 triangles");
    ecs_os_free(out);

    out = box_command_run(world, s, "geometry.check tolerance=0.02", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar a and b: area 1.0000, gap 0.01000, facing (0.00, 1.00, 0.00) at "
        "(0.00, 1.00, 0.00)\n"
        "1 coplanar pair in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_area(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.5f, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check area=2.5", &error);
    test_bool(error, false);
    test_str(out, "no problems in 2 shapes, 24 triangles");
    ecs_os_free(out);

    out = box_command_run(world, s, "geometry.check area=1.5", &error);
    test_bool(error, false);
    test_assert(strstr(out, "1 coplanar pair in 2 shapes") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_max(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {4, 1, 4});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.75f, 0});
    ecs_set(world, b, Box, {2, 0.5f, 2});
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_set(world, c, Position, {0, 0.75f, 0});
    ecs_set(world, c, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check max=1", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar a and b: area 4.0000, gap 0.00000, facing (0.00, 1.00, 0.00) at "
        "(0.00, 1.00, 0.00)\n"
        "warning coplanar: 2 more like the ones above\n"
        "3 coplanar pairs in 3 shapes, 36 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_root(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t ship = ecs_entity(world, { .name = "ship" });
    ecs_entity_t a = ecs_entity(world, { .name = "a", .parent = ship });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b", .parent = ship });
    ecs_set(world, b, Position, {0, 0.75f, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_set(world, c, Position, {10, 0.5f, 0});
    ecs_set(world, c, Box, {2, 1, 2});
    ecs_entity_t d = ecs_entity(world, { .name = "d" });
    ecs_set(world, d, Position, {10, 0.5f, 0});
    ecs_set(world, d, Box, {2, 1, 2});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check ship", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar ship.a and ship.b: area 1.0000, gap 0.00000, facing (0.00, 1.00, "
        "0.00) at (0.00, 1.00, 0.00)\n"
        "1 coplanar pair in 2 shapes, 24 triangles");
    ecs_os_free(out);

    out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_assert(strstr(out, "warning coplanar c and d: area 16.0000") != NULL);
    test_assert(strstr(out, "2 coplanar pairs in 4 shapes, 48 triangles") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_mirrored(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_set(world, parent, Scale, {-1, 1, 1});
    ecs_entity_t a = ecs_entity(world, { .name = "a", .parent = parent });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b", .parent = parent });
    ecs_set(world, b, Position, {0.25f, 0.75f, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out,
        "warning coplanar parent.a and parent.b: area 1.0000, gap 0.00000, facing (0.00, "
        "1.00, 0.00) at (-0.25, 1.00, 0.00)\n"
        "1 coplanar pair in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_rotated(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_set(world, parent, Position, {3, -2, 5});
    ecs_set(world, parent, Rotation, {0.3f, 0.5f, 0.2f});
    ecs_entity_t a = ecs_entity(world, { .name = "a", .parent = parent });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b", .parent = parent });
    ecs_set(world, b, Position, {0.25f, 0.75f, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_assert(strstr(out, "warning coplanar parent.a and parent.b: area 1.0000, gap 0.0000")
        != NULL);
    test_assert(strstr(out, "1 coplanar pair in 2 shapes, 24 triangles") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_tilted(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.75f, 0});
    ecs_set(world, b, Rotation, {0.035f, 0, 0});
    ecs_set(world, b, Box, {1, 0.5f, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 2 shapes, 24 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_cylinder_caps(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.5f, 0});
    ecs_set(world, b, Cylinder, { .segments = 24, .length = 1, .radius = 0.5f });
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_assert(strstr(out, "warning coplanar a and b: area 1.5529, gap 0.00000") != NULL);
    test_assert(strstr(out, "1 coplanar pair in 2 shapes") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_skips_2d(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0, 0});
    ecs_set(world, a, Rect, {2, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0, 0});
    ecs_set(world, b, Rect, {1, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 0 shapes, 0 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_skips_prefabs(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_add_id(world, a, EcsPrefab);
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {0, 0.5f, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_add_id(world, c, EcsDisabled);
    ecs_set(world, c, Position, {0, 0.5f, 0});
    ecs_set(world, c, Box, {2, 1, 2});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 1 shapes, 12 triangles");
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_errors(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0);

    bool error = false;
    char *out = box_command_run(world, s, "geometry.check foo=1", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid option 'foo=1'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "geometry.check tolerance=-1", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid value in 'tolerance=-1'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "geometry.check nothing", &error);
    test_bool(error, true);
    test_assert(strstr(out, "unknown entity 'nothing'") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_status(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_set(world, b, Position, {4, 0, 0});
    ecs_set(world, b, Box, {1, 1, 1});
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_set(world, c, Rect, {1, 1});
    ecs_progress(world, 0);

    bool error = true;
    char *out = box_command_run(world, s, "geometry", &error);
    test_bool(error, false);
    test_assert(strstr(out, "2 3D shapes, 24 triangles, 1 2D shapes, ") == out);
    ecs_os_free(out);

    ecs_fini(world);
}

void GeometryCheck_unplaced(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_set(world, a, Position, {0, 0.5f, 0});
    ecs_set(world, a, Box, {2, 1, 2});
    ecs_entity_t b = ecs_entity(world, { .name = "b", .parent = a });
    ecs_set(world, b, Box, {2, 1, 2});
    ecs_progress(world, 0);

    test_assert(ecs_get(world, b, WorldTransform) == NULL);

    bool error = true;
    char *out = box_command_run(world, s, "geometry.check", &error);
    test_bool(error, false);
    test_str(out, "no problems in 1 shapes, 12 triangles; 1 shapes without a WorldTransform "
        "are not drawn and not checked");
    ecs_os_free(out);

    ecs_fini(world);
}
