/**
 * @file Search.c
 * @brief Tests for the query, iter, next, done, has, grep and grep_recur commands.
 */

#include <box.h>
#include <bake_test.h>
#include <command.h>
#include <string.h>

void Search_query_create(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_set(world, e1, Position, {1, 2, 3});
    ecs_set(world, e2, Position, {4, 5, 6});

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "query q box.transform.Position, (ChildOf, parent)",
        &error);
    test_bool(error, false);
    test_str(out, "2 results");
    ecs_os_free(out);

    ecs_entity_t q = ecs_lookup_child(world, s, "q");
    test_assert(q != 0);
    test_assert(ecs_query_get(world, q) != NULL);

    ecs_fini(world);
}

void Search_query_one_result(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "query q (ChildOf, parent)", NULL);
    test_str(out, "1 result");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_invalid(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "query q NoSuchComponent", &error);
    test_bool(error, true);
    test_str(out, "error: invalid query 'NoSuchComponent'");
    ecs_os_free(out);
    test_assert(ecs_lookup_child(world, s, "q") == 0);

    ecs_fini(world);
}

void Search_query_invalid_name(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "query a.b box.transform.Position", &error);
    test_bool(error, true);
    test_str(out, "error: invalid query name 'a.b'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_show(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q box.transform.Position", NULL));

    char *out = box_terminal_run(world, s, "query q", NULL);
    test_str(out, "box.transform.Position");
    ecs_os_free(out);

    bool error = false;
    out = box_terminal_run(world, s, "query r", &error);
    test_bool(error, true);
    test_str(out, "error: unknown query 'r'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_list(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "query", NULL);
    test_str(out, "");
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "query a box.transform.Position", NULL));
    ecs_os_free(box_terminal_run(world, s, "query b box.movement.Velocity", NULL));

    out = box_terminal_run(world, s, "query", NULL);
    test_str(out, "a: box.transform.Position\nb: box.movement.Velocity");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_replace(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_set(world, e1, Position, {1, 2, 3});
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_entity_t first = ecs_lookup_child(world, s, "q");

    char *out = box_terminal_run(world, s,
        "query q (ChildOf, parent), box.transform.Position", NULL);
    test_str(out, "1 result");
    ecs_os_free(out);
    test_assert(!ecs_is_alive(world, first));

    out = box_terminal_run(world, s, "query q", NULL);
    test_str(out, "(ChildOf, parent), box.transform.Position");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_per_session(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s1, "query q box.transform.Position", NULL));

    bool error = false;
    char *out = box_terminal_run(world, s2, "iter q", &error);
    test_bool(error, true);
    test_str(out, "error: unknown query 'q'");
    ecs_os_free(out);

    out = box_terminal_run(world, s1, "iter q", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_deleted_with_session(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q box.transform.Position", NULL));
    ecs_entity_t q = ecs_lookup_child(world, s, "q");
    test_assert(q != 0);

    ecs_delete(world, s);
    test_assert(!ecs_is_alive(world, q));

    ecs_fini(world);
}

void Search_iter_unknown(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "iter q", &error);
    test_bool(error, true);
    test_str(out, "error: unknown query 'q'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    test_str(out, "error: not iterating, use iter first");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_iter_next(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_entity_t e3 = ecs_entity(world, { .name = "e3", .parent = parent });
    ecs_set(world, e1, Position, {1, 2, 3});
    ecs_set(world, e2, Position, {4, 5, 6});
    ecs_set(world, e3, Position, {7, 8, 9});

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s,
        "query q box.transform.Position, (ChildOf, parent)", NULL));

    char *out = box_terminal_run(world, s, "iter q", NULL);
    test_str(out, "3 results");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", NULL);
    test_str(out, "parent.e1 {x: 1, y: 2, z: 3}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", NULL);
    test_str(out, "parent.e2 {x: 4, y: 5, z: 6}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", NULL);
    test_str(out, "parent.e3 {x: 7, y: 8, z: 9}\n(end)");
    ecs_os_free(out);

    bool error = false;
    out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    test_str(out, "error: not iterating, use iter first");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_next_count(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_entity(world, { .name = "e3", .parent = parent });
    ecs_entity(world, { .name = "e4", .parent = parent });
    ecs_entity(world, { .name = "e5", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    char *out = box_terminal_run(world, s, "next 2", NULL);
    test_str(out, "parent.e1\nparent.e2");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next 2", NULL);
    test_str(out, "parent.e3\nparent.e4");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next 10", NULL);
    test_str(out, "parent.e5\n(end)");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_next_exact_end(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    char *out = box_terminal_run(world, s, "next 2", NULL);
    test_str(out, "parent.e1\nparent.e2\n(end)");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_next_empty_query(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "parent" });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "query q (ChildOf, parent)", NULL);
    test_str(out, "0 results");
    ecs_os_free(out);
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    out = box_terminal_run(world, s, "next", NULL);
    test_str(out, "(end)");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_other_command_ends_iteration(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_entity(world, { .name = "e3", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    ecs_os_free(box_terminal_run(world, s, "next", NULL));
    ecs_os_free(box_terminal_run(world, s, "query", NULL));

    bool error = false;
    char *out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    test_str(out, "error: not iterating, use iter first");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_failed_command_ends_iteration(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    ecs_os_free(box_terminal_run(world, s, "nosuchcommand", NULL));

    bool error = false;
    char *out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_quiet_command_ends_iteration(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    ecs_os_free(box_terminal_run(world, s, "commands", NULL));

    bool error = false;
    char *out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_iteration_per_session(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_entity(world, { .name = "e3", .parent = parent });

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s1, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s1, "iter q", NULL));
    ecs_os_free(box_terminal_run(world, s1, "next", NULL));
    ecs_os_free(box_terminal_run(world, s2, "query", NULL));

    char *out = box_terminal_run(world, s1, "next", NULL);
    test_str(out, "parent.e2");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_iter_restarts(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    ecs_os_free(box_terminal_run(world, s, "next", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    char *out = box_terminal_run(world, s, "next", NULL);
    test_str(out, "parent.e1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_done(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    bool error = true;
    char *out = box_terminal_run(world, s, "done", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "done", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_query_deleted_while_iterating(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    ecs_delete(world, ecs_lookup_child(world, s, "q"));

    bool error = false;
    char *out = box_terminal_run(world, s, "next", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_component(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_set(world, e, Position, {1, 2, 3});

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "has e box.transform.Position", &error);
    test_bool(error, false);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "has e box.movement.Velocity", &error);
    test_bool(error, false);
    test_str(out, "false");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_pair(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "child", .parent = parent });
    ecs_entity(world, { .name = "other" });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "has parent.child (ChildOf, parent)", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "has other (ChildOf, parent)", NULL);
    test_str(out, "false");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_expression(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e1 = ecs_entity(world, { .name = "e1" });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2" });
    ecs_set(world, e1, Position, {1, 2, 3});
    ecs_set(world, e2, Position, {1, 2, 3});
    ecs_set(world, e2, Velocity, {1, 2, 3});

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s,
        "has e1 box.transform.Position, !box.movement.Velocity", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_terminal_run(world, s,
        "has e2 box.transform.Position, !box.movement.Velocity", NULL);
    test_str(out, "false");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_named_query(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e1 = ecs_entity(world, { .name = "e1" });
    ecs_entity(world, { .name = "e2" });
    ecs_set(world, e1, Velocity, {1, 2, 3});

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query moving box.movement.Velocity", NULL));

    char *out = box_terminal_run(world, s, "has e1 moving", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "has e2 moving", NULL);
    test_str(out, "false");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_relative_to_cwd(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t child = ecs_entity(world, { .name = "child", .parent = parent });
    ecs_set(world, child, Position, {1, 2, 3});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "has child box.transform.Position", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_has_invalid(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "e" });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "has e NoSuchComponent", &error);
    test_bool(error, true);
    test_str(out, "error: invalid query 'NoSuchComponent'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "has nosuchentity box.transform.Position", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_name(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "apple", .parent = parent });
    ecs_entity(world, { .name = "pineapple", .parent = parent });
    ecs_entity(world, { .name = "pear", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    bool error = true;
    char *out = box_terminal_run(world, s, "grep apple", &error);
    test_bool(error, false);
    test_str(out, "parent.apple\nparent.pineapple");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_component_name(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_set(world, e1, Velocity, {1, 2, 3});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep Velocity", NULL);
    test_str(out, "parent.e1:box.movement.Velocity");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_value(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_set(world, e1, Position, {1, 42, 3});
    ecs_set(world, e2, Position, {4, 5, 6});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep 42", NULL);
    test_str(out, "parent.e1:box.transform.Position: {x: 1, y: 42, z: 3}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "grep \"y: 5\"", NULL);
    test_str(out, "parent.e2:box.transform.Position: {x: 4, y: 5, z: 6}");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_long_value(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e = ecs_entity(world, { .name = "e", .parent = parent });
    ecs_doc_set_brief(world, e,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "needle"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep needle", NULL);
    test_str(out, "parent.e:(flecs.doc.Description,flecs.doc.Brief): "
        "...aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "needle"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb...");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_component_filter(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t e1 = ecs_entity(world, { .name = "e7", .parent = parent });
    ecs_entity_t e2 = ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_set(world, e1, Position, {7, 0, 0});
    ecs_set(world, e1, Velocity, {7, 0, 0});
    ecs_set(world, e2, Velocity, {7, 0, 0});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep box.transform.Position 7", NULL);
    test_str(out, "parent.e7:box.transform.Position: {x: 7, y: 0, z: 0}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "grep box.movement.Velocity Velocity", NULL);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_unknown_component(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "grep NoSuchComponent 1", &error);
    test_bool(error, true);
    test_str(out, "error: unknown component 'NoSuchComponent'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_no_match(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    bool error = true;
    char *out = box_terminal_run(world, s, "grep zzz", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_not_recursive(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t mid = ecs_entity(world, { .name = "mid_x", .parent = parent });
    ecs_entity(world, { .name = "leaf_x", .parent = mid });

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep _x", NULL);
    test_str(out, "parent.mid_x");
    ecs_os_free(out);

    ecs_set(world, s, Terminal, { .cwd = "parent.mid_x" });
    out = box_terminal_run(world, s, "grep _x", NULL);
    test_str(out, "parent.mid_x.leaf_x");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_root(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "unique_root_name" });
    ecs_entity(world, { .name = "unique_child_name", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "grep unique_", NULL);
    test_str(out, "unique_root_name");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_recur(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t mid = ecs_entity(world, { .name = "mid_x", .parent = parent });
    ecs_entity_t leaf = ecs_entity(world, { .name = "leaf", .parent = mid });
    ecs_entity(world, { .name = "a_x", .parent = parent });
    ecs_set(world, leaf, Position, {0, 0, 99});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep_recur _x", NULL);
    test_str(out, "parent.a_x\nparent.mid_x");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "grep_recur 99", NULL);
    test_str(out, "parent.mid_x.leaf:box.transform.Position: {x: 0, y: 0, z: 99}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "grep 99", NULL);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_recur_root(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "unique_root_name" });
    ecs_entity(world, { .name = "unique_child_name", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "grep_recur unique_", NULL);
    test_str(out, "unique_root_name\nunique_root_name.unique_child_name");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_recur_component(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity_t mid = ecs_entity(world, { .name = "mid", .parent = parent });
    ecs_entity_t leaf = ecs_entity(world, { .name = "leaf", .parent = mid });
    ecs_set(world, mid, Velocity, {0, 0, 5});
    ecs_set(world, leaf, Position, {0, 0, 5});
    ecs_set(world, leaf, Velocity, {0, 1, 0});

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep_recur box.transform.Position 5", NULL);
    test_str(out, "parent.mid.leaf:box.transform.Position: {x: 0, y: 0, z: 5}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "grep_recur box.movement.Velocity 5", NULL);
    test_str(out, "parent.mid:box.movement.Velocity: {x: 0, y: 0, z: 5}");
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_grep_limit(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    int32_t i;
    for (i = 0; i < 120; i ++) {
        char name[16];
        ecs_os_snprintf(name, sizeof(name), "n%03d", i);
        ecs_entity(world, { .name = name, .parent = parent });
    }

    ecs_entity_t s = box_terminal_new(world);
    ecs_set(world, s, Terminal, { .cwd = "parent" });
    char *out = box_terminal_run(world, s, "grep n", NULL);
    test_assert(strstr(out, "parent.n000\nparent.n001\n") == out);
    test_assert(strstr(out, "parent.n099\n... 20 more") != NULL);
    test_assert(strstr(out, "parent.n100") == NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Search_commands_are_script_functions(void) {
    // command/search
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {"query", "iter", "next", "done", "has", "grep", "grep_recur"};
    int32_t i;
    for (i = 0; i < 7; i ++) {
        char *path = flecs_asprintf("box.command.commands.%s", names[i]);
        ecs_entity_t f = ecs_lookup(world, path);
        test_assert(f != 0);
        test_assert(ecs_has(world, f, EcsScriptFunction));
        ecs_os_free(path);
    }

    ecs_fini(world);
}
