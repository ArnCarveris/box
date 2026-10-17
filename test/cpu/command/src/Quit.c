/**
 * @file Quit.c
 * @brief Tests for the quit command.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include <sys/stat.h>

/** Frame at which QuitAfter runs the quit command, and the session it runs it in */
typedef struct {
    ecs_entity_t session;   /**< Session that runs quit */
    int32_t at;             /**< Frame of the system that runs quit */
    int32_t frames;         /**< Frames the system ran */
} quit_after_t;

/** Run quit in a session once the system has run a number of frames.
 *
 * @param it The iterator
 */
static void QuitAfter(
    ecs_iter_t *it)
{
    quit_after_t *q = it->ctx;
    q->frames ++;
    if (q->frames == q->at) {
        ecs_os_free(box_terminal_run(it->world, q->session, "quit", NULL));
    }
}

void Quit_empty_output_ends_progress(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    test_bool(ecs_progress(world, 0), true);

    bool error = true;
    char *out = box_terminal_run(world, s, "quit", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    test_bool(ecs_should_quit(world), true);
    test_bool(ecs_progress(world, 0), false);

    ecs_fini(world);
}

void Quit_logged(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "quit", NULL));

    char *out = box_terminal_run(world, s, "commands", NULL);
    test_str(out, "$ quit");
    ecs_os_free(out);

    test_int(ecs_get(world, s, TerminalStatus)->commands, 2);

    ecs_fini(world);
}

void Quit_http_exec(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=quit", ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, "");
    ecs_os_free(body);
    ecs_os_free(path);

    test_bool(ecs_progress(world, 0), false);

    ecs_fini(world);
}

void Quit_headless_run_ends_early(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_set(world, App, {
        .headless = true, .frames = 1000, .cpu_budget = 100,
        .unguarded = true, .unpaced = true
    });

    const AppStatus *status = ecs_singleton_get(world, AppStatus);
    test_assert(status->tmp_dir != NULL);
    char *root = ecs_os_strdup(status->tmp_dir);

    quit_after_t q = { .session = box_terminal_new(world), .at = 5 };
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "QuitAfter" }),
        .phase = EcsOnUpdate,
        .run = QuitAfter,
        .ctx = &q,
        .immediate = true
    });

    test_int(box_app_run(world), 0);
    test_int(q.frames, 5);
    test_int(ecs_singleton_get(world, AppStatus)->frame, 5);
    test_bool(ecs_singleton_get(world, AppStatus)->running, false);

    struct stat st;
    test_assert(stat(root, &st) == 0);
    ecs_fini(world);
    test_assert(stat(root, &st) != 0);

    ecs_os_free(root);
}

void Quit_script_call(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_script_run(world, "quit",
        "const out = box.command.commands.quit(0)\n", NULL), 0);
    test_bool(ecs_should_quit(world), true);
    test_bool(ecs_progress(world, 0), false);

    ecs_fini(world);
}

void Quit_listed_and_script_function(void) {
    // command/quit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t f = ecs_lookup(world, "box.command.commands.quit");
    test_assert(f != 0);
    test_assert(ecs_has(world, f, EcsScriptFunction));

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "ls /box.command.commands", NULL);
    test_assert(strstr(out, "\nquit\n") != NULL);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "man quit", NULL);
    test_str(out, "usage: quit\nbrief: End the run after this frame");
    ecs_os_free(out);

    test_bool(ecs_should_quit(world), false);

    ecs_fini(world);
}
