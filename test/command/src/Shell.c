/**
 * @file Shell.c
 * @brief Tests for the entity shell commands cd, ls, touch, rm, mv and man.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

void Shell_cd_child(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "cd a", &error);
    test_bool(error, false);
    test_str(out, "/a");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "cd b", &error);
    test_bool(error, false);
    test_str(out, "/a.b");
    test_str(ecs_get(world, s, Terminal)->cwd, "a.b");
    test_assert(ecs_lookup(world, ecs_get(world, s, Terminal)->cwd) == b);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_cd_parent(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.b.c" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a.b.c", NULL));
    char *out = box_terminal_run(world, s, "cd ..", NULL);
    test_str(out, "/a.b");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "cd ../..", NULL);
    test_str(out, "/");
    test_assert(ecs_get(world, s, Terminal)->cwd == NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_cd_parent_of_root(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "cd ..", &error);
    test_bool(error, false);
    test_str(out, "/");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_cd_root(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a.b", NULL));
    char *out = box_terminal_run(world, s, "cd", NULL);
    test_str(out, "/");
    test_assert(ecs_get(world, s, Terminal)->cwd == NULL);
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "cd a.b", NULL));
    out = box_terminal_run(world, s, "cd /", NULL);
    test_str(out, "/");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_cd_absolute(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.b" });
    ecs_entity(world, { .name = "c.d" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a.b", NULL));
    char *out = box_terminal_run(world, s, "cd /c.d", NULL);
    test_str(out, "/c.d");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_cd_unknown(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    bool error = false;
    char *out = box_terminal_run(world, s, "cd nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    test_str(ecs_get(world, s, Terminal)->cwd, "a");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_cwd(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.c" });
    ecs_entity(world, { .name = "a.b" });
    ecs_entity(world, { .name = "a.b.x" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    bool error = true;
    char *out = box_terminal_run(world, s, "ls", &error);
    test_bool(error, false);
    test_str(out, "b\nc");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_path(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.b.y" });
    ecs_entity(world, { .name = "a.b.x" });
    ecs_entity(world, { .name = "c" });
    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "ls a.b", NULL);
    test_str(out, "x\ny");
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "cd c", NULL));
    out = box_terminal_run(world, s, "ls ../a", NULL);
    test_str(out, "b");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "ls /a.b", NULL);
    test_str(out, "x\ny");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_root(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "zebra.child" });
    ecs_entity(world, { .name = "apple" });
    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "ls", NULL);
    test_assert(strstr(out, "apple\n") != NULL);
    test_assert(strstr(out, "flecs\n") != NULL);
    test_assert(strstr(out, "zebra") != NULL);
    test_assert(strstr(out, "child") == NULL);
    test_assert(strstr(out, "apple") < strstr(out, "zebra"));
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_anonymous(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t anon = ecs_new_w_pair(world, EcsChildOf, a);
    ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "ls a", NULL);
    char *expect = flecs_asprintf("b\n#%u", (uint32_t)anon);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_empty(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "ls a", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_unknown(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "ls nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_ls_many(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    int32_t i;
    for (i = 0; i < 300; i ++) {
        ecs_new_w_pair(world, EcsChildOf, a);
    }

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "ls a", NULL);
    test_assert(strstr(out, "\n... 44 more") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_touch(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "touch a", &error);
    test_bool(error, false);
    test_str(out, "");
    test_assert(ecs_lookup(world, "a") != 0);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_touch_nested(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "touch a.b.c", NULL));
    ecs_entity_t c = ecs_lookup(world, "a.b.c");
    test_assert(c != 0);
    test_assert(ecs_get_parent(world, c) == ecs_lookup(world, "a.b"));

    ecs_fini(world);
}

void Shell_touch_relative(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a.b", NULL));
    ecs_os_free(box_terminal_run(world, s, "touch c", NULL));
    ecs_os_free(box_terminal_run(world, s, "touch ../d", NULL));
    ecs_os_free(box_terminal_run(world, s, "touch /e", NULL));
    test_assert(ecs_lookup(world, "a.b.c") != 0);
    test_assert(ecs_lookup(world, "a.d") != 0);
    test_assert(ecs_lookup_child(world, 0, "e") != 0);

    ecs_fini(world);
}

void Shell_touch_existing(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_add_id(world, a, EcsPrefab);
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "touch a", &error);
    test_bool(error, false);
    test_assert(ecs_lookup(world, "a") == a);
    test_assert(ecs_has_id(world, a, EcsPrefab));
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_touch_invalid(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "touch a..b", &error);
    test_bool(error, true);
    test_str(out, "error: invalid path 'a..b'");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "touch /", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "touch *", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_rm(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "rm a", &error);
    test_bool(error, false);
    test_str(out, "");
    test_bool(ecs_is_alive(world, a), false);
    test_bool(ecs_is_alive(world, b), false);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_rm_relative(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    ecs_os_free(box_terminal_run(world, s, "rm b", NULL));
    test_bool(ecs_is_alive(world, b), false);
    test_bool(ecs_is_alive(world, a), true);

    ecs_fini(world);
}

void Shell_rm_unknown(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "rm nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_rm_root(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "rm /", &error);
    test_bool(error, true);
    test_str(out, "error: '/' is the root");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_rm_builtin(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "rm flecs.core", &error);
    test_bool(error, true);
    test_str(out, "error: 'flecs.core' is builtin");
    test_assert(ecs_is_alive(world, EcsFlecsCore));
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_rm_module(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "rm box.command", &error);
    test_bool(error, true);
    test_str(out, "error: 'box.command' belongs to a module or session");
    ecs_os_free(out);

    char *path = ecs_get_path(world, s);
    char *line = flecs_asprintf("rm %s", path);
    out = box_terminal_run(world, s, line, &error);
    test_bool(error, true);
    test_bool(ecs_is_alive(world, s), true);
    ecs_os_free(out);
    ecs_os_free(line);
    ecs_os_free(path);

    ecs_fini(world);
}

void Shell_rm_panic(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_add_pair(world, b, EcsOnDelete, EcsPanic);
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "rm a", &error);
    test_bool(error, true);
    test_str(out, "error: 'a' may not be deleted");
    test_bool(ecs_is_alive(world, a), true);
    ecs_os_free(out);

    ecs_remove_pair(world, b, EcsOnDelete, EcsPanic);
    ecs_fini(world);
}

void Shell_mv_rename(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "p.a" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mv p.a p.b", &error);
    test_bool(error, false);
    test_str(out, "");
    test_assert(ecs_lookup(world, "p.b") == a);
    test_assert(ecs_lookup(world, "p.a") == 0);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_mv_into_existing(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mv a c", NULL));
    test_assert(ecs_lookup(world, "c.a") == a);
    test_assert(ecs_get_parent(world, a) == c);

    ecs_fini(world);
}

void Shell_mv_new_path(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t x = ecs_entity(world, { .name = "a.x" });
    ecs_entity(world, { .name = "c" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mv a c.d", NULL));
    test_assert(ecs_lookup(world, "c.d") == a);
    test_assert(ecs_lookup(world, "c.d.x") == x);

    ecs_fini(world);
}

void Shell_mv_to_root(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t b = ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "cd a", NULL));
    ecs_os_free(box_terminal_run(world, s, "mv b /", NULL));
    test_assert(ecs_get_parent(world, b) == 0);
    test_assert(ecs_lookup_child(world, 0, "b") == b);

    ecs_fini(world);
}

void Shell_mv_parent_component(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t p = ecs_entity(world, { .name = "p" });
    ecs_entity_t q = ecs_entity(world, { .name = "q" });
    ecs_entity_t a = ecs_new(world);
    ecs_set(world, a, EcsParent, { .value = p });
    ecs_set_name(world, a, "a");
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "mv p.a q", &error);
    test_bool(error, false);
    test_assert(ecs_get_parent(world, a) == q);
    test_assert(ecs_owns(world, a, EcsParent));
    test_assert(ecs_lookup(world, "q.a") == a);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_mv_conflict(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t c = ecs_entity(world, { .name = "c" });
    ecs_entity(world, { .name = "c.a" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mv a c", &error);
    test_bool(error, true);
    test_str(out, "error: 'c' exists");
    test_assert(ecs_get_parent(world, a) == 0);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mv a c.b.d", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'c.b'");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "c") == c);
    ecs_fini(world);
}

void Shell_mv_into_self(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity(world, { .name = "a.b" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mv a a.b", &error);
    test_bool(error, true);
    test_str(out, "error: cannot move 'a' into itself");
    test_assert(ecs_get_parent(world, a) == 0);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_mv_into_module(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mv a flecs.core", &error);
    test_bool(error, true);
    test_str(out, "error: cannot move into 'flecs.core'");
    test_assert(ecs_get_parent(world, a) == 0);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_man_entity(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_doc_set_brief(world, a, "An entity");
    ecs_doc_set_detail(world, a, "More about it");
    ecs_doc_set_link(world, a, "https://example.com");
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "man a", &error);
    test_bool(error, false);
    test_str(out, "brief: An entity\ndetail: More about it\nlink: https://example.com");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_man_doc_name(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t a = ecs_entity(world, { .name = "a" });
    ecs_doc_set_name(world, a, "The A");
    ecs_doc_set_color(world, a, "#ff0000");
    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "man a", NULL);
    test_str(out, "name: The A\ncolor: #ff0000");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_man_command(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "man set", NULL);
    test_str(out, "usage: set <entity> <component> <value...>\n"
        "brief: Assign a flecs script value to a component or member like Position.x");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "man ls", NULL);
    test_str(out, "usage: ls [path]\nbrief: List the children of the current entity or a path");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_man_no_docs(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a" });
    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "man a", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_man_unknown(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "man nope", &error);
    test_bool(error, true);
    test_str(out, "error: no entity 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_sessions_independent(void) {
    // command/shell
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "a.x" });
    ecs_entity(world, { .name = "b.x" });
    ecs_entity_t s1 = box_terminal_new(world);
    ecs_entity_t s2 = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s1, "cd a", NULL));
    ecs_os_free(box_terminal_run(world, s2, "cd b", NULL));
    ecs_os_free(box_terminal_run(world, s1, "touch y", NULL));
    ecs_os_free(box_terminal_run(world, s2, "touch z", NULL));
    test_assert(ecs_lookup(world, "a.y") != 0);
    test_assert(ecs_lookup(world, "b.z") != 0);
    test_assert(ecs_lookup(world, "a.z") == 0);

    ecs_fini(world);
}
