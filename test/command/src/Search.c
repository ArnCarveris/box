/**
 * @file Search.c
 * @brief Tests for the query, iteration and grep commands of box.command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <stdio.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;
static ecs_entity_t point;
static ecs_entity_t tag;

void Search_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);

    point = ecs_struct(w, {
        .entity = ecs_entity(w, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_i32_t) },
            { .name = "y", .type = ecs_id(ecs_i32_t) }
        }
    });

    tag = ecs_entity(w, { .name = "Tag" });

    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Search_teardown(void)
{
    ecs_fini(w);
}

void Search_query_creates_query(void)
{
    // command/search
    char *out = box_command_run(w, session, "query q Point", NULL);
    test_str(out, "agent0.q");
    ecs_os_free(out);

    ecs_entity_t q = ecs_lookup(w, "agent0.q");
    test_assert(q != 0);
    test_assert(ecs_get_parent(w, q) == session);
    test_assert(ecs_query_get(w, q) != NULL);
}

void Search_query_replaces_query(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });
    ecs_add_id(w, b, tag);

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "query q Tag", NULL);
    test_str(out, "agent0.q");
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next 10", NULL);
    test_str(out, "scene.b\n(no more)\n");
    ecs_os_free(out);
}

void Search_query_invalid_expression(void)
{
    // command/search
    bool error = false;
    char *out = box_command_run(w, session, "query q NotAComponent", &error);
    test_bool(error, true);
    test_str(out, "invalid query 'NotAComponent'");
    ecs_os_free(out);

    test_assert(ecs_lookup(w, "agent0.q") == 0);
}

void Search_iter_and_next_in_chunks(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_entity_t c = ecs_entity(w, { .name = "scene.c" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 3, 4 });
    ecs_set_id(w, c, point, sizeof(int32_t) * 2, (int32_t[]){ 5, 6 });

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    bool error = true;
    out = box_command_run(w, session, "iter q", &error);
    test_bool(error, false);
    test_str(out, "agent0.q");
    ecs_os_free(out);

    out = box_command_run(w, session, "next 2", NULL);
    test_str(out, "scene.a\t{x: 1, y: 2}\nscene.b\t{x: 3, y: 4}\n");
    ecs_os_free(out);

    out = box_command_run(w, session, "next", NULL);
    test_str(out, "scene.c\t{x: 5, y: 6}\n");
    ecs_os_free(out);
}

void Search_next_past_the_end(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next 10", NULL);
    test_str(out, "scene.a\t{x: 1, y: 2}\n(no more)\n");
    ecs_os_free(out);

    bool error = false;
    out = box_command_run(w, session, "next", &error);
    test_bool(error, true);
    test_str(out, "no active iterator");
    ecs_os_free(out);
}

void Search_next_without_iter(void)
{
    // command/search
    bool error = false;
    char *out = box_command_run(w, session, "next", &error);
    test_bool(error, true);
    test_str(out, "no active iterator");
    ecs_os_free(out);
}

void Search_done_finishes_iterator(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 3, 4 });

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next", NULL);
    test_str(out, "scene.a\t{x: 1, y: 2}\n");
    ecs_os_free(out);

    bool error = true;
    out = box_command_run(w, session, "done", &error);
    test_bool(error, false);
    test_str(out, "agent0.q");
    ecs_os_free(out);

    out = box_command_run(w, session, "next", &error);
    test_bool(error, true);
    test_str(out, "no active iterator");
    ecs_os_free(out);
}

void Search_other_command_exits_iteration(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 3, 4 });

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next", NULL);
    test_str(out, "scene.a\t{x: 1, y: 2}\n");
    ecs_os_free(out);

    out = box_command_run(w, session, "ls scene", NULL);
    ecs_os_free(out);

    bool error = false;
    out = box_command_run(w, session, "next", &error);
    test_bool(error, true);
    test_str(out, "no active iterator");
    ecs_os_free(out);
}

void Search_grep_matches_names(void)
{
    // command/search
    ecs_entity(w, { .name = "scene.alpha" });
    ecs_entity(w, { .name = "scene.beta" });

    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "grep alp", NULL);
    test_str(out, "scene.alpha\n");
    ecs_os_free(out);
}

void Search_grep_matches_component_names(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity(w, { .name = "scene.b" });
    ecs_add_id(w, a, tag);

    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "grep Tag", NULL);
    test_str(out, "scene.a\tTag\n");
    ecs_os_free(out);
}

void Search_grep_matches_values(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 10, 20 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 30, 40 });

    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "grep 20", NULL);
    test_str(out, "scene.a\tPoint\t{x: 10, y: 20}\n");
    ecs_os_free(out);
}

void Search_grep_with_component_filter(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 10, 20 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 30, 40 });
    ecs_add_id(w, a, tag);

    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "grep Point 30", NULL);
    test_str(out, "scene.b\tPoint\t{x: 30, y: 40}\n");
    ecs_os_free(out);

    out = box_command_run(w, session, "grep Point a", NULL);
    test_str(out, "");
    ecs_os_free(out);
}

void Search_grep_unknown_component(void)
{
    // command/search
    bool error = false;
    char *out = box_command_run(w, session, "grep Nope 10", &error);
    test_bool(error, true);
    test_str(out, "unknown component 'Nope'");
    ecs_os_free(out);
}

void Search_grep_recur_nested_children(void)
{
    // command/search
    ecs_entity_t deep = ecs_entity(w, { .name = "scene.a.deep" });
    ecs_set_id(w, deep, point, sizeof(int32_t) * 2, (int32_t[]){ 7, 8 });

    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "grep 7", NULL);
    test_str(out, "");
    ecs_os_free(out);

    out = box_command_run(w, session, "grep_recur 7", NULL);
    test_str(out, "scene.a.deep\tPoint\t{x: 7, y: 8}\n");
    ecs_os_free(out);
}

void Search_output_capped_at_limit(void)
{
    // command/search
    int32_t i;
    for (i = 0; i < 150; i ++) {
        char name[32];
        sprintf(name, "scene.e%03d", i);
        ecs_entity_t e = ecs_entity(w, { .name = name });
        ecs_set_id(w, e, point, sizeof(int32_t) * 2, (int32_t[]){ i, i });
    }

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next 150", NULL);
    int32_t lines = 0;
    const char *p;
    for (p = out; p[0]; p ++) {
        if (p[0] == '\n') {
            lines ++;
        }
    }

    test_int(lines, 101);
    test_assert(strstr(out, "\n... 50 more\n") != NULL);
    test_assert(strstr(out, "scene.e000\t{x: 0, y: 0}\n") == out);
    ecs_os_free(out);

    out = box_command_run(w, session, "next 60", NULL);
    lines = 0;
    for (p = out; p[0]; p ++) {
        if (p[0] == '\n') {
            lines ++;
        }
    }

    test_int(lines, 51);
    test_assert(strstr(out, "(no more)\n") != NULL);
    ecs_os_free(out);
}

void Search_delete_session_while_iterating(void)
{
    // command/search
    ecs_entity_t a = ecs_entity(w, { .name = "scene.a" });
    ecs_entity_t b = ecs_entity(w, { .name = "scene.b" });
    ecs_set_id(w, a, point, sizeof(int32_t) * 2, (int32_t[]){ 1, 2 });
    ecs_set_id(w, b, point, sizeof(int32_t) * 2, (int32_t[]){ 3, 4 });

    char *out = box_command_run(w, session, "query q Point", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "iter q", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "next", NULL);
    test_str(out, "scene.a\t{x: 1, y: 2}\n");
    ecs_os_free(out);

    ecs_delete(w, session);
    test_assert(!ecs_is_alive(w, session));
    test_assert(ecs_lookup(w, "agent0.q") == 0);
}
