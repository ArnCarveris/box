/**
 * @file Complete.c
 * @brief Tests for the completion of command names and entity paths.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Complete_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Complete_teardown(void)
{
    ecs_fini(w);
}

void Complete_unique_command_name_is_finished(void)
{
    // ui/console
    int32_t caret = 4;
    char *line = box_command_complete(w, session, "capt", &caret, NULL);
    test_assert(line != NULL);
    test_str(line, "capture ");
    test_int(caret, 8);
    ecs_os_free(line);
}

void Complete_command_group_finishes_to_the_shared_prefix(void)
{
    // ui/console
    int32_t caret = 2;
    char *listing = NULL;
    char *line = box_command_complete(w, session, "ke", &caret, &listing);
    test_assert(line != NULL);
    test_str(line, "key.");
    test_int(caret, 4);
    test_assert(listing != NULL);
    test_assert(strstr(listing, "key.press") != NULL);
    test_assert(strstr(listing, "key.type") != NULL);
    ecs_os_free(listing);
    ecs_os_free(line);
}

void Complete_unknown_word_completes_to_nothing(void)
{
    // ui/console
    int32_t caret = 3;
    char *line = box_command_complete(w, session, "zzz", &caret, NULL);
    test_assert(line == NULL);
    test_int(caret, 3);
}

void Complete_argument_completes_an_entity_path(void)
{
    // ui/console
    ecs_entity_t scene = ecs_entity(w, { .name = "scene" });
    ecs_entity(w, { .name = "crate", .parent = scene });

    int32_t caret = 10;
    char *line = box_command_complete(w, session, "ls scene.c", &caret, NULL);
    test_assert(line != NULL);
    test_str(line, "ls scene.crate ");
    ecs_os_free(line);
}

void Complete_argument_lists_the_children_of_a_scope(void)
{
    // ui/console
    ecs_entity_t scene = ecs_entity(w, { .name = "scene" });
    ecs_entity(w, { .name = "crate", .parent = scene });
    ecs_entity(w, { .name = "cube", .parent = scene });

    int32_t caret = 9;
    char *listing = NULL;
    char *line = box_command_complete(w, session, "ls scene.", &caret, &listing);
    test_assert(line != NULL);
    test_str(line, "ls scene.c");
    test_assert(listing != NULL);
    test_assert(strstr(listing, "scene.crate") != NULL);
    test_assert(strstr(listing, "scene.cube") != NULL);
    ecs_os_free(listing);
    ecs_os_free(line);
}

void Complete_word_of_the_caret_keeps_the_rest_of_the_line(void)
{
    // ui/console
    int32_t caret = 4;
    char *line = box_command_complete(w, session, "capt 1 2", &caret, NULL);
    test_assert(line != NULL);
    test_str(line, "capture  1 2");
    test_int(caret, 8);
    ecs_os_free(line);
}

void Complete_argument_resolves_against_the_working_entity(void)
{
    // ui/console
    ecs_entity_t scene = ecs_entity(w, { .name = "scene" });
    ecs_entity(w, { .name = "crate", .parent = scene });
    Terminal *terminal = ecs_get_mut(w, session, Terminal);
    ecs_os_free(terminal->cwd);
    terminal->cwd = ecs_os_strdup("scene");

    int32_t caret = 5;
    char *line = box_command_complete(w, session, "ls cr", &caret, NULL);
    test_assert(line != NULL);
    test_str(line, "ls crate ");
    ecs_os_free(line);
}
