/**
 * @file Shell.c
 * @brief Tests for the entity shell and component commands of box.command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;
static ecs_entity_t point;
static ecs_entity_t tag;
static ecs_entity_t likes;
static ecs_entity_t bob;

void Shell_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);

    point = ecs_struct(w, {
        .entity = ecs_entity(w, { .name = "Point" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_i32_t) },
            { .name = "y", .type = ecs_id(ecs_i32_t) }
        }
    });

    tag = ecs_entity(w, { .name = "Tag" });
    likes = ecs_entity(w, { .name = "Likes" });
    bob = ecs_entity(w, { .name = "Bob" });

    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Shell_teardown(void)
{
    ecs_fini(w);
}

void Shell_cd_changes_cwd(void)
{
    // command/shell
    ecs_entity(w, { .name = "parent.child" });
    bool error = true;
    char *out = box_command_run(w, session, "cd parent", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);

    const Terminal *t = ecs_get(w, session, Terminal);
    test_str(t->cwd, "parent");
}

void Shell_cd_parent(void)
{
    // command/shell
    ecs_entity(w, { .name = "parent.child" });
    char *out = box_command_run(w, session, "cd parent.child", NULL);
    test_str(out, "parent.child");
    ecs_os_free(out);

    out = box_command_run(w, session, "cd ..", NULL);
    test_str(out, "parent");
    ecs_os_free(out);
}

void Shell_cd_root(void)
{
    // command/shell
    ecs_entity(w, { .name = "parent" });
    char *out = box_command_run(w, session, "cd parent", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "cd /", NULL);
    test_str(out, "/");
    ecs_os_free(out);

    const Terminal *t = ecs_get(w, session, Terminal);
    test_assert(t->cwd == NULL);
}

void Shell_cd_unknown_entity(void)
{
    // command/shell
    bool error = false;
    char *out = box_command_run(w, session, "cd nope", &error);
    test_bool(error, true);
    test_str(out, "unknown entity 'nope'");
    ecs_os_free(out);
}

void Shell_ls_lists_children(void)
{
    // command/shell
    ecs_entity(w, { .name = "scene.b" });
    ecs_entity(w, { .name = "scene.a" });
    char *out = box_command_run(w, session, "ls scene", NULL);
    test_str(out, "a\nb\n");
    ecs_os_free(out);
}

void Shell_ls_shows_components(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "scene.a" });
    ecs_add_id(w, e, tag);
    ecs_add_pair(w, e, likes, bob);
    char *out = box_command_run(w, session, "ls scene", NULL);
    test_str(out, "a\tTag, (Likes,Bob)\n");
    ecs_os_free(out);
}

void Shell_ls_relative_to_cwd(void)
{
    // command/shell
    ecs_entity(w, { .name = "scene.a" });
    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "ls", NULL);
    test_str(out, "a\n");
    ecs_os_free(out);
}

void Shell_touch_creates_entity(void)
{
    // command/shell
    char *out = box_command_run(w, session, "touch thing", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "thing") != 0);
}

void Shell_touch_nested_path(void)
{
    // command/shell
    char *out = box_command_run(w, session, "touch a.b.c", NULL);
    test_str(out, "a.b.c");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "a.b.c") != 0);
    test_assert(ecs_lookup(w, "a.b") != 0);
}

void Shell_rm_deletes_entity(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "rm thing", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(!ecs_is_alive(w, e));
}

void Shell_mv_renames_entity(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "mv thing other", NULL);
    test_str(out, "other");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "other") != 0);
    test_assert(ecs_lookup(w, "thing") == 0);
}

void Shell_mv_reparents_entity(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_entity(w, { .name = "scene" });
    char *out = box_command_run(w, session, "mv thing scene.thing", NULL);
    test_str(out, "scene.thing");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "scene.thing") == e);
}

void Shell_mv_existing_target(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    ecs_entity(w, { .name = "other" });
    bool error = false;
    char *out = box_command_run(w, session, "mv thing other", &error);
    test_bool(error, true);
    test_str(out, "'other' already exists");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "thing") != 0);
}

void Shell_add_component(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "add thing Tag", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(ecs_has_id(w, e, tag));
}

void Shell_add_pair(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "add thing (Likes,Bob)", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(ecs_has_pair(w, e, likes, bob));
}

void Shell_add_unknown_component(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    bool error = false;
    char *out = box_command_run(w, session, "add thing Nope", &error);
    test_bool(error, true);
    test_str(out, "unknown component 'Nope'");
    ecs_os_free(out);
}

void Shell_remove_component(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_add_id(w, e, tag);
    char *out = box_command_run(w, session, "remove thing Tag", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(!ecs_has_id(w, e, tag));
}

void Shell_set_component_value(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "set thing Point {10,20}", NULL);
    test_str(out, "thing");
    ecs_os_free(out);

    const int32_t *ptr = ecs_get_id(w, e, point);
    test_assert(ptr != NULL);
    test_int(ptr[0], 10);
    test_int(ptr[1], 20);
}

void Shell_set_invalid_value(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    bool error = false;
    char *out = box_command_run(w, session, "set thing Point nonsense", &error);
    test_bool(error, true);
    test_str(out, "invalid value 'nonsense' for 'Point'");
    ecs_os_free(out);
    test_assert(!ecs_has_id(w, e, point));
}

void Shell_get_component_value(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    char *out = box_command_run(w, session, "set thing Point {10,20}", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "get thing Point", NULL);
    test_str(out, "{x: 10, y: 20}");
    ecs_os_free(out);
}

void Shell_get_all_components(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_add_id(w, e, tag);
    char *out = box_command_run(w, session, "set thing Point {10,20}", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "get thing", NULL);
    test_str(out, "Point: {x: 10, y: 20}\nTag\n");
    ecs_os_free(out);
}

void Shell_get_missing_component(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    bool error = false;
    char *out = box_command_run(w, session, "get thing Point", &error);
    test_bool(error, true);
    test_str(out, "'thing' has no value of 'Point'");
    ecs_os_free(out);
}

void Shell_clear_removes_components(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_add_id(w, e, tag);
    char *out = box_command_run(w, session, "clear thing", NULL);
    test_str(out, "thing");
    ecs_os_free(out);
    test_assert(!ecs_has_id(w, e, tag));
    test_assert(ecs_lookup(w, "thing") == 0);
}

void Shell_has_component(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_add_id(w, e, tag);
    char *out = box_command_run(w, session, "has thing Tag", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_command_run(w, session, "has thing Point", NULL);
    test_str(out, "false");
    ecs_os_free(out);
}

void Shell_has_query_expression(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_add_id(w, e, tag);
    char *out = box_command_run(w, session, "has thing Tag,!Point", NULL);
    test_str(out, "true");
    ecs_os_free(out);

    out = box_command_run(w, session, "has thing Tag,Point", NULL);
    test_str(out, "false");
    ecs_os_free(out);
}

void Shell_has_invalid_query(void)
{
    // command/shell
    ecs_entity(w, { .name = "thing" });
    bool error = false;
    char *out = box_command_run(w, session, "has thing Nope,!Point", &error);
    test_bool(error, true);
    test_str(out, "invalid query 'Nope,!Point'");
    ecs_os_free(out);
}

void Shell_man_prints_doc(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "thing" });
    ecs_doc_set_brief(w, e, "A thing");
    ecs_doc_set_detail(w, e, "Details of the thing");
    ecs_doc_set_link(w, e, "http://example.com");
    char *out = box_command_run(w, session, "man thing", NULL);
    test_str(out, "A thing\nDetails of the thing\nhttp://example.com\n");
    ecs_os_free(out);
}

void Shell_man_command_brief(void)
{
    // command/shell
    char *out = box_command_run(w, session, "man touch", NULL);
    test_str(out, "Create the entity of a path\n");
    ecs_os_free(out);
}

void Shell_man_unknown_entity(void)
{
    // command/shell
    bool error = false;
    char *out = box_command_run(w, session, "man nope", &error);
    test_bool(error, true);
    test_str(out, "unknown entity 'nope'");
    ecs_os_free(out);
}

void Shell_unknown_entity_argument(void)
{
    // command/shell
    bool error = false;
    char *out = box_command_run(w, session, "get nope Point", &error);
    test_bool(error, true);
    test_str(out, "unknown entity 'nope'");
    ecs_os_free(out);
}

void Shell_relative_to_cwd(void)
{
    // command/shell
    ecs_entity_t e = ecs_entity(w, { .name = "scene.thing" });
    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "add thing Tag", NULL);
    test_str(out, "scene.thing");
    ecs_os_free(out);
    test_assert(ecs_has_id(w, e, tag));
}

void Shell_absolute_path(void)
{
    // command/shell
    ecs_entity(w, { .name = "scene.thing" });
    char *out = box_command_run(w, session, "cd scene", NULL);
    ecs_os_free(out);

    out = box_command_run(w, session, "touch .other", NULL);
    test_str(out, "other");
    ecs_os_free(out);
    test_assert(ecs_lookup(w, "other") != 0);
}

void Shell_root_is_not_an_entity(void)
{
    // command/shell
    bool error = false;
    char *out = box_command_run(w, session, "get /", &error);
    test_bool(error, true);
    test_str(out, "the root has no components");
    ecs_os_free(out);
}
