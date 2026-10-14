/**
 * @file Component.c
 * @brief Tests for the component commands add, remove, set, get and clear.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Component_add_tag(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "add e Tag", &error);
    test_bool(error, false);
    test_str(out, "");
    test_assert(ecs_has_id(world, e, tag));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_pair(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t likes = ecs_entity(world, { .name = "Likes" });
    ecs_entity_t bob = ecs_entity(world, { .name = "people.Bob" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "add e (Likes, people.Bob)", &error);
    test_bool(error, false);
    test_assert(ecs_has_pair(world, e, likes, bob));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_relative(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    ecs_os_free(box_terminal_run(world, s, "add b Tag", NULL));
    test_assert(ecs_has_id(world, b, tag));

    ecs_fini(world);
}

void Component_add_short_name(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "add e Scale", &error);
    test_bool(error, false);
    test_assert(ecs_has(world, e, Scale));
    ecs_os_free(out);

    out = box_terminal_run(world, s, "remove e box.transform.Scale", &error);
    test_bool(error, false);
    test_assert(!ecs_has(world, e, Scale));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_module_child(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t mod = ecs_entity(world, { .name = "game" });
    ecs_add_id(world, mod, EcsModule);
    ecs_entity_t tag = ecs_entity(world, { .name = "game.Marker" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "add e Marker", &error);
    test_bool(error, false);
    test_assert(ecs_has_id(world, e, tag));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_unknown_component(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add e Nope", &error);
    test_bool(error, true);
    test_str(out, "error: unknown component 'Nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_unknown_entity(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "Tag" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add nope Tag", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_builtin(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "Tag" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add flecs.core.Component Tag", &error);
    test_bool(error, true);
    test_str(out, "error: 'flecs.core.Component' is builtin");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_childof(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity(world, { .name = "p" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add e (ChildOf, p)", &error);
    test_bool(error, true);
    test_str(out, "error: use mv to change the parent");
    test_assert(ecs_get_parent(world, e) == 0);
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_isa_cycle(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_entity(world, { .name = "b" });
    ecs_add_pair(world, b, EcsIsA, a);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add a (IsA, b)", &error);
    test_bool(error, true);
    test_str(out, "error: '(IsA, b)' creates a cycle");
    test_assert(!ecs_has_pair(world, a, EcsIsA, b));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_add_trait_in_use(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add Tag Sparse", &error);
    test_bool(error, true);
    test_str(out, "error: cannot change 'Sparse' while it is in use");
    test_assert(!ecs_has_id(world, tag, EcsSparse));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_remove(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "remove e Tag", &error);
    test_bool(error, false);
    test_str(out, "");
    test_assert(!ecs_has_id(world, e, tag));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_remove_pair(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t likes = ecs_entity(world, { .name = "Likes" });
    ecs_entity_t bob = ecs_entity(world, { .name = "Bob" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_add_pair(world, e, likes, bob);
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "remove e (Likes,Bob)", NULL));
    test_assert(!ecs_has_pair(world, e, likes, bob));

    ecs_fini(world);
}

void Component_remove_missing(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "Tag" });
    ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "remove e Tag", &error);
    test_bool(error, true);
    test_str(out, "error: 'e' has no 'Tag'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "set e Point {x: 1, y: 2}", &error);
    test_bool(error, false);
    test_str(out, "");
    const float *p = ecs_get_id(world, e, pos);
    test_assert(p != NULL);
    test_flt(p[0], 1);
    test_flt(p[1], 2);
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set_expression(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "set e Point {1 + 2, 2 * 3}", NULL));
    const float *p = ecs_get_id(world, e, pos);
    test_assert(p != NULL);
    test_flt(p[0], 3);
    test_flt(p[1], 6);

    ecs_fini(world);
}

void Component_set_member(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "set e Point {x: 1, y: 2}", NULL));
    bool error = true;
    char *out = box_terminal_run(world, s, "set e Point.y 5", &error);
    test_bool(error, false);
    const float *p = ecs_get_id(world, e, pos);
    test_flt(p[0], 1);
    test_flt(p[1], 5);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "set e Point.z 5", &error);
    test_bool(error, true);
    test_str(out, "error: unknown member 'Point.z'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set_relative(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    ecs_os_free(box_terminal_run(world, s, "set b Point {3, 4}", NULL));
    const float *p = ecs_get_id(world, b, pos);
    test_assert(p != NULL);
    test_flt(p[0], 3);
    test_flt(p[1], 4);

    ecs_fini(world);
}

void Component_set_invalid_value(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    ecs_log_set_level(-5);
    char *out = box_terminal_run(world, s, "set e Point {z: 1}", &error);
    ecs_log_set_level(-1);
    test_bool(error, true);
    test_str(out, "error: invalid value '{z: 1}' for 'Point'");
    test_assert(!ecs_has_id(world, e, pos));
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set_tag(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "Tag" });
    ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "set e Tag 1", &error);
    test_bool(error, true);
    test_str(out, "error: 'Tag' has no value");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set_box_component(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "set e Position {x: 1, y: 2, z: 3}", &error);
    test_bool(error, false);
    test_assert(ecs_has(world, e, Position));
    test_flt(ecs_get(world, e, Position)->z, 3);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "get e Position.z", &error);
    test_bool(error, false);
    test_str(out, "3");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_get(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    float v[] = {1, 2};
    ecs_set_id(world, e, pos, sizeof(v), v);
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "get e Point", &error);
    test_bool(error, false);
    test_str(out, "{x: 1, y: 2}");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "get e Point.x", &error);
    test_bool(error, false);
    test_str(out, "1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_get_all(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "p.e" });
    float v[] = {1, 2};
    ecs_set_id(world, e, pos, sizeof(v), v);
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "get p.e", &error);
    test_bool(error, false);
    test_str(out, "Point: {x: 1, y: 2}\nTag");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_get_missing(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "get e Point", &error);
    test_bool(error, true);
    test_str(out, "error: 'e' has no 'Point'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_get_tag(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "get e Tag", &error);
    test_bool(error, true);
    test_str(out, "error: 'Tag' has no value");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_get_unknown_component(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "e" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "get e Nope", &error);
    test_bool(error, true);
    test_str(out, "error: unknown component 'Nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_clear(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t pos = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });
    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t p = ecs_entity(world, { .name = "p" });
    ecs_entity_t e = ecs_entity(world, { .name = "p.e" });
    float v[] = {1, 2};
    ecs_set_id(world, e, pos, sizeof(v), v);
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "clear p.e", &error);
    test_bool(error, false);
    test_str(out, "");
    test_assert(!ecs_has_id(world, e, pos));
    test_assert(!ecs_has_id(world, e, tag));
    test_assert(ecs_get_parent(world, e) == p);
    test_str(ecs_get_name(world, e), "e");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_clear_in_use(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Tag" });
    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_add_id(world, e, tag);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "clear Tag", &error);
    test_bool(error, true);
    test_str(out, "error: cannot clear 'Tag' while it is in use");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_clear_module(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "clear box.command", &error);
    test_bool(error, true);
    test_str(out, "error: 'box.command' belongs to a module or session");
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_remove_other_session_terminal(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s1, "remove /box.command.terminals.s2 Terminal", &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.terminals.s2' belongs to a module or session");
    ecs_os_free(out);

    test_assert(ecs_has(world, s2, Terminal));
    test_assert(ecs_has(world, s2, TerminalStatus));
    out = box_terminal_run(world, s2, "commands", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_fini(world);
}

void Component_set_other_session_terminal(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "e" });
    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s1,
        "set /box.command.terminals.s2 Terminal {cwd: \"e\"}", &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.terminals.s2' belongs to a module or session");
    ecs_os_free(out);

    out = box_terminal_run(world, s1, "set /box.command.terminals.s2 TerminalStatus.dir \"/\"",
        &error);
    test_bool(error, true);
    ecs_os_free(out);

    const Terminal *t = ecs_get(world, s2, Terminal);
    test_assert(t->cwd == NULL || !t->cwd[0]);
    test_assert(strcmp(ecs_get(world, s2, TerminalStatus)->dir, "/") != 0);

    ecs_fini(world);
}

void Component_add_other_session(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Marker" });
    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s1, "add /box.command.terminals.s2 Marker", &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.terminals.s2' belongs to a module or session");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, s2, tag));

    ecs_fini(world);
}

void Component_clear_other_session(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s1, "clear /box.command.terminals.s2", &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.terminals.s2' belongs to a module or session");
    ecs_os_free(out);
    test_assert(ecs_has(world, s2, Terminal));

    ecs_fini(world);
}

void Component_remove_terminal_host(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s,
        "remove /box.command.TerminalHost box.command.TerminalHost", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s,
        "clear /box.command.TerminalHost", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands 1", &error);
    test_bool(error, false);
    ecs_os_free(out);
    test_assert(box_terminal_new(world) != 0);
    ecs_progress(world, 0);

    ecs_fini(world);
}

void Component_add_module_singleton(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t tag = ecs_entity(world, { .name = "Marker" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "add /box.command.TerminalLimits Marker", &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.TerminalLimits' belongs to a module or session");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, ecs_id(TerminalLimits), tag));

    out = box_terminal_run(world, s,
        "remove /box.command.TerminalLimits TerminalLimits", &error);
    test_bool(error, true);
    ecs_os_free(out);
    test_assert(ecs_singleton_get(world, TerminalLimits) != NULL);

    ecs_fini(world);
}

void Component_set_module_singleton(void) {
    // hardening/session_isolation
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s,
        "set /box.command.TerminalLimits TerminalLimits.sessions 3", &error);
    test_bool(error, false);
    ecs_os_free(out);
    test_int(ecs_singleton_get(world, TerminalLimits)->sessions, 3);

    out = box_terminal_run(world, s, "set /box.command.TerminalLimits Position {1, 2, 3}",
        &error);
    test_bool(error, true);
    test_str(out, "error: '/box.command.TerminalLimits' belongs to a module or session");
    ecs_os_free(out);
    test_assert(!ecs_has(world, ecs_id(TerminalLimits), Position));

    ecs_fini(world);
}
