/**
 * @file Shell.c
 * @brief Tests for the box commands in the flecs script shell and box_command_run.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <output.h>
#include "command.h"

/** Delta time of a frame of the audio tests, 800 frames at 48000 Hz */
#define SHELL_AUDIO_DT (1.0f / 60.0f)

/** Rate of the headless output */
#define SHELL_AUDIO_RATE 48000

void Shell_box_scope_first(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const EcsScriptShell *shell = ecs_singleton_get(world, EcsScriptShell);
    test_assert(shell != NULL);
    test_assert(ecs_vec_count(&shell->command_paths) > 1);
    test_uint(ecs_vec_first_t(&shell->command_paths, ecs_entity_t)[0],
        ecs_lookup(world, BOX_COMMAND_SCOPE));
    test_assert(ecs_lookup(world, "flecs.script.shell.commands.core") != 0);

    ecs_fini(world);
}

void Shell_commands_are_script_functions(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *names[] = {
        "key_press", "key_down", "key_up", "key_repeat", "key_type", "mouse_move",
        "mouse_sweep", "mouse_release", "mouse_button", "mouse_click", "mouse_drag",
        "mouse_scroll", "action_press", "action_down", "action_up", "action_drag",
        "action_list", "input_reset", "input_focus", "input_wait", "clipboard_set",
        "clipboard_get", "capture", "render", "fps", "frames", "edit", "logs"
    };

    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(names) / sizeof(names[0])); i ++) {
        ecs_entity_t f = ecs_lookup_child(world, ecs_lookup(world, BOX_COMMAND_SCOPE), names[i]);
        test_assert(f != 0);
        test_assert(ecs_has(world, f, EcsScriptFunction));
        test_assert(ecs_doc_get_brief(world, f) != NULL);
    }

    ecs_fini(world);
}

void Shell_box_command_overrides_builtin(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "key_down A", &error);
    test_bool(error, false);
    test_str(out, "1");
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyA].down, true);

    ecs_fini(world);
}

void Shell_builtin_commands(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "parent.a" });
    ecs_entity(world, { .name = "parent.b" });

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "cd parent", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);
    test_uint(ecs_get(world, s, EcsCommandSession)->scope, parent);

    out = box_command_run(world, s, "ls", &error);
    test_bool(error, false);
    test_str(out, "parent.a\nparent.b");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_string_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "clipboard_set \"hello world\"", NULL));

    bool error = true;
    char *out = box_command_run(world, s, "clipboard_get", &error);
    test_bool(error, false);
    test_str(out, "hello world");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_entity_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity(world, { .name = "parent.child" });

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    char *out = box_command_run(world, s, "pwd", NULL);
    test_str(out, "#0");
    ecs_os_free(out);

    ecs_os_free(box_command_run(world, s, "cd parent.child", NULL));
    out = box_command_run(world, s, "pwd", NULL);
    test_str(out, "parent.child");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_value_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_entity(world, { .name = "e" });
    ecs_set(world, e, Position, {1, 2, 3});

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "get e box.transform.Position", &error);
    test_bool(error, false);
    test_str(out, "{\"Position\":{\"x\":1, \"y\":2, \"z\":3}}");
    ecs_os_free(out);

    out = box_command_run(world, s, "set e box.transform.Position {y: 5}", &error);
    test_bool(error, false);
    ecs_os_free(out);

    const Position *p = ecs_get(world, e, Position);
    test_flt(p->x, 1);
    test_flt(p->y, 5);
    test_flt(p->z, 3);

    ecs_fini(world);
}

void Shell_run_no_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "clipboard_set text", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_unknown_command(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown command 'nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_command_error(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "key_press Nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown key 'Nope'");
    ecs_os_free(out);

    test_int(box_input_pending(world), 0);

    ecs_fini(world);
}

void Shell_run_multi_results(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    // Each key_press returns the frames until the input queue is drained
    char *out = box_command_run(world, s, "key_press W 2; key_press A 1", &error);
    test_bool(error, false);
    test_str(out, "3\n4");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_multi_no_result(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "clipboard_set text; key_press W 2", &error);
    test_bool(error, false);
    test_str(out, "3");
    ecs_os_free(out);

    error = true;
    // The queue of the first line is still pending, the key is released two frames later
    out = box_command_run(world, s, "key_press W 2; clipboard_set text", &error);
    test_bool(error, false);
    test_str(out, "5");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_multi_error(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "key_press W 2; key_press Nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown key 'Nope'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_missing_argument(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "key_press", &error);
    test_bool(error, true);
    test_assert(strstr(out, BOX_COMMAND_ERROR "missing argument 'key'") == out);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_run_not_a_session(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t e = ecs_new(world);
    bool error = false;
    char *out = box_command_run(world, e, "input_wait", &error);
    test_bool(error, true);
    test_assert(strstr(out, "is not a command session") != NULL);
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_format_vector(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t vec = ecs_vector(world, { .type = ecs_id(ecs_i32_t) });
    ecs_vec_t v = {0};
    ecs_vec_append_t(NULL, &v, int32_t)[0] = 10;
    ecs_vec_append_t(NULL, &v, int32_t)[0] = 20;

    char *out = box_command_format(world, &(ecs_value_t){ .type = vec, .ptr = &v });
    test_str(out, "10\n20");
    ecs_os_free(out);

    out = box_command_format(world, &(ecs_value_t){0});
    test_str(out, "");
    ecs_os_free(out);

    ecs_vec_fini_t(NULL, &v, int32_t);
    ecs_fini(world);
}

void Shell_run_text_unchanged(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity(world, { .name = "things.alpha.leaf" });
    ecs_entity(world, { .name = "things.beta" });
    ecs_entity_t cube = ecs_entity(world, { .name = "cube" });
    ecs_set(world, cube, Position, {1, 2, 3});

    // The console colors its own output, sessions keep the plain text: full paths, JSON for
    // structured results, help without groups and errors without color codes
    ecs_os_free(box_console_run(world, "tree things", NULL));
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "ls things", &error);
    test_bool(error, false);
    test_str(out, "things.alpha\nthings.beta");
    ecs_os_free(out);

    out = box_command_run(world, s, "tree things", &error);
    test_bool(error, false);
    test_str(out,
        "{\"entity\":\"things.alpha\", \"depth\":0}\n"
        "{\"entity\":\"things.alpha.leaf\", \"depth\":1}\n"
        "{\"entity\":\"things.beta\", \"depth\":0}");
    ecs_os_free(out);

    out = box_command_run(world, s, "get cube box.transform.Position", &error);
    test_bool(error, false);
    test_str(out, "{\"Position\":{\"x\":1, \"y\":2, \"z\":3}}");
    ecs_os_free(out);

    out = box_command_run(world, s, "help ls", &error);
    test_bool(error, false);
    test_str(out, "ls [scope]  Direct children\n"
        "    scope: Scope to list (default: session scope)");
    ecs_os_free(out);

    out = box_command_run(world, s, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\n\n") == NULL);
    test_assert(strstr(out, "console only") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "key_press Nope", &error);
    test_bool(error, true);
    test_str(out, BOX_COMMAND_ERROR "unknown key 'Nope'");
    ecs_os_free(out);

    // The session of the console too
    out = box_command_run(world, ecs_lookup(world, BOX_CONSOLE_SESSION), "ls things", &error);
    test_bool(error, false);
    test_str(out, "things.alpha\nthings.beta");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_help_lists_box_commands(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "key_press <key> [dur]  Tap a key, held for dur frames") != NULL);
    test_assert(strstr(out, "capture [camera]  Write a PNG") != NULL);
<<<<<<< HEAD
    test_assert(strstr(out, "audio [action] [value] [arg] [vel] [dur]  Print the audio status") != NULL);
=======
    test_assert(strstr(out, "audio [action] [value] [args...]  Print the audio status") != NULL);
>>>>>>> 1cde799b5b3139d3ab3a75b563906ec8f7299e0e
    test_assert(strstr(out, "\nls [scope]  ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "help edit", &error);
    test_bool(error, false);
    test_str(out, "edit [state]  Open or close the editor, toggle without a state\n"
        "    state: on or off (default: toggle)");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_history(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "input_wait", NULL));
    ecs_os_free(box_command_run(world, s, "nope", NULL));

    const EcsCommandSession *cs = ecs_get(world, s, EcsCommandSession);
    test_int(ecs_vec_count(&cs->history), 2);
    test_str(ecs_vec_get_t(&cs->history, char*, 0)[0], "input_wait");
    test_str(ecs_vec_get_t(&cs->history, char*, 1)[0], "nope");

    ecs_fini(world);
}

void Shell_script_call(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script_run(world, "test", "const pending = "
        "box.command.commands.key_press(\"W\", 3)\n", NULL) == 0);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyW].down, true);

    ecs_fini(world);
}

void Shell_rest_endpoint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_http_server_t *srv = ecs_rest_server_init(world, NULL);
    test_assert(srv != NULL);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", "/shell/session", NULL, &reply), 0);
    test_int(reply.code, 200);
    char *body = ecs_strbuf_get(&reply.body);
    unsigned long long session = 0;
    test_int(sscanf(body, "{\"session\":%llu}", &session), 1);
    ecs_os_free(body);

    char *path = flecs_asprintf("/shell/session/%llu", session);
    reply = (ecs_http_reply_t)ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", path, "key_press W 2", &reply), 0);
    test_int(reply.code, 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "{\"results\":[{\"type\":\"flecs.meta.i32\", \"result\":3}], "
        "\"errors\":[]}");
    ecs_os_free(body);

    reply = (ecs_http_reply_t)ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", path, "key_press Nope", &reply), 0);
    test_int(reply.code, 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "{\"results\":[{\"type\":\"flecs.meta.i32\", \"result\":0}], "
        "\"errors\":[\"unknown key 'Nope'\"]}");
    ecs_os_free(body);

    ecs_os_free(path);
    ecs_rest_server_fini(srv);
    ecs_fini(world);
}

void Shell_quit(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_bool(ecs_progress(world, 0), true);

    bool error = true;
    char *out = box_command_run(world, s, "quit", &error);
    test_bool(error, false);
    test_str(out, "");
    ecs_os_free(out);

    test_bool(ecs_should_quit(world), true);
    test_bool(ecs_progress(world, 0), false);

    ecs_fini(world);
}

void Shell_console_session(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });

    bool error = true;
    char *out = box_console_run(world, "cd parent", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);

    ecs_entity_t session = ecs_lookup(world, BOX_CONSOLE_SESSION);
    test_assert(session != 0);
    test_assert(ecs_has(world, session, EcsCommandSession));
    test_uint(ecs_get(world, session, EcsCommandSession)->scope, parent);

    out = box_console_run(world, "pwd", &error);
    test_bool(error, false);
    test_str(out, "parent");
    ecs_os_free(out);

    ecs_fini(world);
}

void Shell_resource_file(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_assert(box_command_resource_file(world, s, "1") == NULL);

    ecs_shell_resource_t r = {0};
    test_int(ecs_shell_resource_create(world, s, "text/plain", "hello", 5, &r), 0);
    test_str(r.name, "1");

    char *file = box_command_resource_file(world, s, r.name);
    char *expect = flecs_asprintf("%s/1", ecs_get(world, s, EcsCommandSession)->resource_path);
    test_str(file, expect);
    FILE *f = fopen(file, "rb");
    test_assert(f != NULL);
    char buf[8] = {0};
    test_int((int)fread(buf, 1, sizeof(buf), f), 5);
    fclose(f);
    test_str(buf, "hello");

    ecs_delete(world, s);
    test_assert(access(file, F_OK) != 0);

    ecs_os_free(expect);
    ecs_os_free(file);
    ecs_os_free(r.name);
    ecs_os_free(r.content_type);
    ecs_fini(world);
}

void Shell_audio_command(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t bus = ecs_entity(world, { .name = "music" });
    ecs_entity_t hall = ecs_entity(world, { .name = "hall" });
    ecs_ensure(world, bus, AudioBus)->volume = 0.5f;
    ecs_modified(world, bus, AudioBus);
    ecs_ensure(world, hall, AudioBus)->effects[0] = hall;
    ecs_modified(world, hall, AudioBus);
    ecs_set(world, hall, Reverb, { .decay = 2.4f, .damping = 0.4f, .predelay = 0.02f,
        .size = 0.5f, .mix = 1 });
    ecs_entity_t echo = ecs_entity(world, { .name = "echo" });
    ecs_ensure(world, echo, AudioBus)->effects[0] = echo;
    ecs_modified(world, echo, AudioBus);
    ecs_set(world, echo, Delay, { .sync = 0.75f, .feedback = 0.4f, .mix = 1,
        .ping_pong = true, .damping = 4000 });
    ecs_entity_t grit = ecs_entity(world, { .name = "grit" });
    ecs_ensure(world, grit, AudioBus)->effects[0] = grit;
    ecs_modified(world, grit, AudioBus);
    ecs_set(world, grit, Saturator, { .drive = 0.6f, .asymmetry = 0.2f, .bits = 8,
        .mix = 0.5f });
    ecs_entity_t fuzz = ecs_entity(world, { .name = "fuzz" });
    ecs_ensure(world, fuzz, AudioBus)->effects[0] = fuzz;
    ecs_modified(world, fuzz, AudioBus);
    ecs_set(world, fuzz, Saturator, { .drive = 0.8f, .bits = 16, .mix = 1,
        .mode = SaturatorHard, .low_cut = 150, .tone = 0.25f, .high_cut = 5000,
        .rate = 8000 });
    ecs_entity_t punch = ecs_entity(world, { .name = "punch" });
    ecs_ensure(world, punch, AudioBus)->effects[0] = punch;
    ecs_modified(world, punch, AudioBus);
    ecs_set(world, punch, TransientShaper, { .attack = 0.5f, .sustain = -0.25f });
    ecs_entity_t talk = ecs_entity(world, { .name = "talk" });
    ecs_ensure(world, talk, AudioBus)->effects[0] = talk;
    ecs_modified(world, talk, AudioBus);
    ecs_set(world, talk, Vocoder, { .modulator = punch, .bands = 24, .low = 120,
        .high = 7000, .q = 1.5f, .attack = 0.004f, .release = 0.06f, .shift = -0.25f,
        .unvoiced = 0.3f, .mix = 0.8f });
    ecs_entity_t lift = ecs_entity(world, { .name = "lift" });
    ecs_ensure(world, lift, AudioBus)->effects[0] = lift;
    ecs_modified(world, lift, AudioBus);
    ecs_set(world, lift, PitchShifter, { .semitones = 12, .cents = -8, .stereo = 1,
        .window = 30, .feedback = 0.2f, .mix = 0.4f });
    ecs_entity_t shine = ecs_entity(world, { .name = "shine" });
    ecs_ensure(world, shine, AudioBus)->effects[0] = shine;
    ecs_modified(world, shine, AudioBus);
    ecs_set(world, shine, Reverb, { .decay = 6, .damping = 0.3f, .predelay = 0.02f,
        .size = 0.8f, .mix = 1, .shimmer = 0.5f, .shimmer_pitch = 12 });
    ecs_entity_t bell = ecs_entity(world, { .name = "bell" });
    ecs_ensure(world, bell, AudioBus)->effects[0] = bell;
    ecs_modified(world, bell, AudioBus);
    ecs_set(world, bell, FrequencyShifter, { .shift = 437, .mode = FrequencyRing,
        .stereo = 0.5f, .feedback = -0.3f, .mix = 0.8f });
    ecs_entity_t chop = ecs_entity(world, { .name = "chop" });
    ecs_ensure(world, chop, AudioBus)->effects[0] = chop;
    ecs_modified(world, chop, AudioBus);
    ecs_set(world, chop, PatternGate, { .steps = { 1, 0, 0.5f, 1 }, .length = 4,
        .rate = 0.25f, .swing = 0.1f, .attack = 0.002f, .release = 0.03f, .gate = 0.5f,
        .floor = -80, .mix = 1 });
    ecs_entity_t organ = ecs_entity(world, { .name = "organ" });
    ecs_ensure(world, organ, AudioBus)->effects[0] = organ;
    ecs_modified(world, organ, AudioBus);
    ecs_set(world, organ, Rotary, { .speed = 1, .horn_slow = 0.8f, .horn_fast = 6.7f,
        .drum_slow = 0.7f, .drum_fast = 5.9f, .acceleration = 1, .balance = 0.2f,
        .spread = 1, .drive = 0.3f, .mix = 1 });
    ecs_entity_t fill = ecs_entity(world, { .name = "fill" });
    ecs_ensure(world, fill, AudioBus)->effects[0] = fill;
    ecs_modified(world, fill, AudioBus);
    ecs_set(world, fill, BeatRepeat, { .slice = 0.125f, .grid = -1, .decay = 0.8f,
        .pitch = -2, .reverse = 1, .gate = 0.5f, .mix = 1 });
    ecs_entity_t drop = ecs_entity(world, { .name = "drop" });
    ecs_ensure(world, drop, AudioBus)->effects[0] = drop;
    ecs_modified(world, drop, AudioBus);
    ecs_set(world, drop, TapeStop, { .active = 1, .stop_time = 1, .start_time = 0.25f,
        .curve = 0.5f });
    ecs_progress(world, 0.016f);

    bool error = true;
    char *out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "off, 0 Hz, 0 voices", 19) == 0);
    test_assert(strstr(out, "\nmaster: volume 1.00") != NULL);
    test_assert(strstr(out, "music: group volume 0.50 pan 0.00 width 1.00 -> master") != NULL);
    test_assert(strstr(out, "hall: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 hall: reverb decay 2.40 damping 0.40 predelay 0.020 size 0.50 mix 1.00 "
        "(does not run: no output)") != NULL);
    test_assert(strstr(out, "echo: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 echo: delay sync 0.750 beats feedback 0.40 damping 4000 Hz mix 1.00 ping pong "
        "(does not run: no output)") != NULL);
    test_assert(strstr(out, "grit: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 grit: saturator drive 0.60 asymmetry 0.20 bits 8.0 mix 0.50") != NULL);
    test_assert(strstr(out, "insert 0 fuzz: saturator hard drive 0.80 asymmetry 0.00 "
        "low cut 150 Hz tone 0.25 high cut 5000 Hz rate 8000 Hz bits off mix 1.00") != NULL);
    test_assert(strstr(out, "punch: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 punch: transient shaper attack 0.50 sustain -0.25 "
        "(does not run: no output)") != NULL);
    test_assert(strstr(out, "talk: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 talk: vocoder modulator punch bands 24 low 120 Hz high 7000 Hz q 1.50 "
        "attack 0.0040 release 0.060 shift -0.25 unvoiced 0.30 mix 0.80 "
        "(does not run: no output)") != NULL);
    test_assert(strstr(out, "lift: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 lift: pitch shifter semitones +12.0 cents -8.0 stereo 1.00 window 30.0 ms "
        "feedback 0.20 mix 0.40 (does not run: no output)") != NULL);
    test_assert(strstr(out, "shine: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 shine: reverb decay 6.00 damping 0.30 predelay 0.020 size 0.80 mix 1.00 "
        "shimmer 0.50 pitch +12.0 (does not run: no output)") != NULL);
    test_assert(strstr(out, "bell: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 bell: frequency shifter ring +437.0 Hz stereo 0.50 feedback -0.30 mix 0.80 "
        "(does not run: no output)") != NULL);
    test_assert(strstr(out, "chop: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 chop: pattern gate steps [1 0 0.5 1] rate 0.250 beats swing 0.10 gate 0.50 "
        "attack 0.0020 release 0.030 floor -80.0 dB mix 1.00") != NULL);
    test_assert(strstr(out, "organ: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 organ: rotary speed 1.00 horn 0.80-6.70 Hz drum 0.70-5.90 Hz "
        "acceleration 1.00 s balance 0.20 spread 1.00 drive 0.30 mix 1.00") != NULL);
    test_assert(strstr(out, "fill: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 fill: beat repeat inactive slice 0.125 beats grid slice decay 0.80 "
        "pitch -2.0 gate 0.50 mix 1.00 reverse (does not run: no output)") != NULL);
    test_assert(strstr(out, "drop: group volume 1.00 pan 0.00 width 1.00 -> master, "
        "insert 0 drop: tape stop active stop 1.000 s start 0.250 s curve +0.50 "
        "(does not run: no output)") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio volume 0.25", &error);
    test_bool(error, false);
    test_str(out, "master volume 0.25");
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, AudioSettings)->master_volume, 0.25f);

    out = box_command_run(world, s, "audio mute on", &error);
    test_str(out, "mute on");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, AudioSettings)->mute, true);
    out = box_command_run(world, s, "audio mute", &error);
    test_str(out, "mute off");
    ecs_os_free(out);

    out = box_command_run(world, s, "audio volume loud", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio sing", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

<<<<<<< HEAD
/** Create a world with a headless audio output and run a script of test/assets/audio in it.
 *
 * @param path The script
 * @return The world
 */
static ecs_world_t* shell_audio_world(
    const char *path)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_audio_output_open(world, &(box_audio_output_desc_t){ .headless = true });
    char *file = box_app_locate(path);
    test_assert(file != NULL);
    test_int(ecs_script_run_file(world, file), 0);
    ecs_os_free(file);
=======
void Shell_audio_analyze(void) {
    // audio/analyze
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    bool error = true;
    char *out = box_command_run(world, s,
        "audio analyze test/assets/audio/analyze_sine_1k.wav", &error);
    test_bool(error, false);
    const char *head = "analyze test/assets/audio/analyze_sine_1k.wav: 48000 Hz, mono, "
        "2.000 s, span 0.000 to 2.000 s\nloudness: integrated -3.0";
    test_assert(strncmp(out, head, strlen(head)) == 0);
    test_assert(strstr(out, "\npeak: sample 0.00 dBFS, true ") != NULL);
    test_assert(strstr(out, ", clipped 0 samples\ndc offset: ") != NULL);
    test_assert(strstr(out, "\nbands: sub ") != NULL);
    test_assert(strstr(out, ", mid 0.0 dB, presence ") != NULL);
    test_assert(strstr(out, "\nspectrum: centroid ") != NULL);
    test_assert(strstr(out, "\nstereo: mono file, correlation +1.00, side/mid -inf dB, "
        "mono loss 0.0 dB") != NULL);
    test_assert(strstr(out, "\nonsets: 1: 0.000 s +") != NULL);
    test_assert(strstr(out, "\nattack: ") != NULL);
    test_assert(strstr(out, "\ntail: cut short") != NULL);
    test_assert(strstr(out, "\nsilence: leading 0.000 s, trailing ") != NULL);
    test_assert(strstr(out, "\nclicks: start none, end ") != NULL);
    test_assert(strstr(out, "\ndiscontinuities: 0\n") != NULL);
    test_assert(strstr(out, "\npitch: 1000.0 Hz B5 +21 cents, spread ") != NULL);
    test_assert(strstr(out, "\nbars:") == NULL);

    char *again = box_command_run(world, s,
        "audio analyze test/assets/audio/analyze_sine_1k.wav", &error);
    test_bool(error, false);
    test_str(again, out);
    ecs_os_free(again);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio analyze test/assets/audio/analyze_sine_1k.wav 0.5 1",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, "2.000 s, span 0.500 to 1.000 s\n") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio analyze test/assets/audio/analyze_click.wav",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nclicks: start 0.500 (-6.0 dBFS), end ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio analyze test/assets/audio/analyze_decay.wav",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, "\ntail: 1.3") != NULL);
    test_assert(strstr(out, " s to -60 dB after the onset at 0.000 s") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_analyze_resource(void) {
    // audio/analyze
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    char *path = box_app_locate("test/assets/audio/analyze_inverted.wav");
    test_assert(path != NULL);
    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    ecs_os_free(path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *bytes = ecs_os_malloc((ecs_size_t)size);
    test_assert(fread(bytes, 1, (size_t)size, f) == (size_t)size);
    fclose(f);

    ecs_shell_resource_t r = {0};
    test_int(ecs_shell_resource_create(world, s, "audio/wav", bytes, (ecs_size_t)size, &r), 0);
    ecs_os_free(bytes);
    char *line = flecs_asprintf("audio analyze %s", r.name);
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    char *head = flecs_asprintf("analyze %s: 48000 Hz, stereo, 1.000 s", r.name);
    test_assert(strncmp(out, head, strlen(head)) == 0);
    test_assert(strstr(out, "\nstereo: correlation -1.00, side/mid ") != NULL);
    test_assert(strstr(out, ", mono loss 100.0 dB") != NULL);
    ecs_os_free(head);
    ecs_os_free(out);
    ecs_os_free(line);
    ecs_os_free(r.name);
    ecs_os_free(r.content_type);

    ecs_shell_resource_t empty = {0};
    test_int(ecs_shell_resource_create(world, s, "audio/wav", NULL, 0, &empty), 0);
    line = flecs_asprintf("audio analyze %s", empty.name);
    out = box_command_run(world, s, line, &error);
    test_bool(error, true);
    test_assert(strstr(out, "is empty") != NULL);
    ecs_os_free(out);
    ecs_os_free(line);
    ecs_os_free(empty.name);
    ecs_os_free(empty.content_type);
    ecs_fini(world);
}

void Shell_audio_analyze_errors(void) {
    // audio/analyze
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    bool error = false;
    char *out = box_command_run(world, s, "audio analyze", &error);
    test_bool(error, true);
    test_assert(strstr(out, "audio analyze needs a WAV") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio analyze test/assets/audio/missing.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot find 'test/assets/audio/missing.wav'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio analyze test/assets/audio/analyze_adpcm.wav",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "unsupported WAV format") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio analyze test/assets/audio/corrupt.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "not a WAV file") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio analyze test/assets/audio/analyze_sine_1k.wav 3 4",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio analyze test/assets/audio/analyze_sine_1k.wav 1 0.5", &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio analyze test/assets/audio/analyze_sine_1k.wav soon", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid span") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** A world with the song of test/assets/audio/chords.flecs, no output and no device */
static ecs_world_t* shell_chords_world(
    ecs_entity_t *session)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *session = ecs_shell_session_init(world, NULL);
    char *path = box_app_locate("test/assets/audio/chords.flecs");
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);
>>>>>>> 1cde799b5b3139d3ab3a75b563906ec8f7299e0e

    return world;
}

<<<<<<< HEAD
/** Run frames of a fixed delta.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void shell_audio_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        test_bool(ecs_progress(world, SHELL_AUDIO_DT), true);
    }
}

/** Run a command that must succeed.
 *
 * @param world The world
 * @param s The session
 * @param line The command line
 * @return The output, which the caller frees
 */
static char* shell_audio_ok(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *line)
{
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    if (error) {
        printf("%s: %s\n", line, out);
    }

    test_bool(error, false);

    return out;
}

/** Start a capture with the audio command.
 *
 * @param world The world
 * @param s The session
 * @param seconds Seconds of the capture, as text
 * @return The name of the resource, which the caller frees
 */
static char* shell_audio_capture(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *seconds)
{
    char *line = flecs_asprintf("audio capture %s", seconds);
    char *result = shell_audio_ok(world, s, line);
    ecs_os_free(line);

    return result;
}

/** Run frames until a capture wrote its resource, and read its samples.
 *
 * @param world The world
 * @param s The session
 * @param name The name of the resource, freed
 * @param frames Receives the number of stereo frames
 * @return The samples, left and right interleaved, which the caller frees
 */
static int16_t* shell_audio_capture_read(
    ecs_world_t *world,
    ecs_entity_t s,
    char *name,
    int32_t *frames)
{
    int32_t i;
    for (i = 0; i < 400 && box_audio_capture_pending(world); i ++) {
        shell_audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);
    char *file = box_command_resource_file(world, s, name);
    test_assert(file != NULL);
    FILE *f = fopen(file, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 44, SEEK_SET);
    test_assert(size > 44);
    int16_t *result = ecs_os_malloc((ecs_size_t)(size - 44));
    test_assert(fread(result, 1, (size_t)(size - 44), f) == (size_t)(size - 44));
    fclose(f);
    *frames = (int32_t)((size - 44) / 4);
    ecs_os_free(file);
    ecs_os_free(name);

    return result;
}

/** Get the peak of a channel of a capture over a range of frames.
 *
 * @param samples The samples, interleaved
 * @param channel 0 for left, 1 for right
 * @param from First frame
 * @param to Frame past the last
 * @return The largest magnitude
 */
static int32_t shell_audio_peak(
    const int16_t *samples,
    int32_t channel,
    int32_t from,
    int32_t to)
{
    int32_t i, result = 0;
    for (i = from; i < to; i ++) {
        int32_t v = samples[i * 2 + channel];
        v = v < 0 ? -v : v;
        result = v > result ? v : result;
    }

    return result;
}

void Shell_audio_solo(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition_buses.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t left = ecs_lookup(world, "left");
    ecs_entity_t right = ecs_lookup(world, "right");
    test_assert(left != 0);
    test_assert(right != 0);

    char *out = shell_audio_ok(world, s, "audio solo left");
    test_str(out, "solo left on");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, true);
    test_bool(ecs_get(world, right, AudioBus)->solo, false);

    out = shell_audio_ok(world, s, "audio");
    test_assert(strstr(out, "\n  left: group volume 1.00 pan -1.00 width 1.00 solo -> master")
        != NULL);
    ecs_os_free(out);

    out = shell_audio_ok(world, s, "audio solo left");
    test_str(out, "solo left off");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, false);

    out = shell_audio_ok(world, s, "audio solo right on");
    test_str(out, "solo right on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio solo left on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio solo none");
    test_str(out, "solo none, 2 cleared");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, false);
    test_bool(ecs_get(world, right, AudioBus)->solo, false);

    bool error = false;
    out = box_command_run(world, s, "audio solo nowhere", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio solo left_source", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio solo left maybe", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_solo_capture(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition_buses.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    int32_t frames = 0;
    shell_audio_run(world, 5);

    char *out = shell_audio_ok(world, s, "audio solo left");
    ecs_os_free(out);
    shell_audio_run(world, 5);
    int16_t *wav = shell_audio_capture_read(world, s,
        shell_audio_capture(world, s, "0.25"), &frames);
    test_int(frames, SHELL_AUDIO_RATE / 4);
    test_assert(shell_audio_peak(wav, 0, 2400, frames) > 3000);
    test_assert(shell_audio_peak(wav, 1, 2400, frames) < 30);
    ecs_os_free(wav);

    out = shell_audio_ok(world, s, "audio solo none");
    ecs_os_free(out);
    shell_audio_run(world, 5);
    wav = shell_audio_capture_read(world, s, shell_audio_capture(world, s, "0.25"), &frames);
    test_assert(shell_audio_peak(wav, 0, 2400, frames) > 3000);
    test_assert(shell_audio_peak(wav, 1, 2400, frames) > 3000);
    ecs_os_free(wav);
    ecs_fini(world);
}

void Shell_audio_mute_bus(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition_buses.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t left = ecs_lookup(world, "left");
    int32_t frames = 0;
    shell_audio_run(world, 5);

    char *out = shell_audio_ok(world, s, "audio mute left");
    test_str(out, "mute left on");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->mute, true);
    test_bool(ecs_singleton_get(world, AudioSettings)->mute, false);

    out = shell_audio_ok(world, s, "audio");
    test_assert(strstr(out, "\n  left: group volume 1.00 pan -1.00 width 1.00 muted -> master")
        != NULL);
    ecs_os_free(out);

    shell_audio_run(world, 5);
    int16_t *wav = shell_audio_capture_read(world, s,
        shell_audio_capture(world, s, "0.25"), &frames);
    test_assert(shell_audio_peak(wav, 0, 2400, frames) < 30);
    test_assert(shell_audio_peak(wav, 1, 2400, frames) > 3000);
    ecs_os_free(wav);

    out = shell_audio_ok(world, s, "audio mute left off");
    test_str(out, "mute left off");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->mute, false);

    out = shell_audio_ok(world, s, "audio mute master on");
    test_str(out, "mute box.audio.master on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio mute master off");
    ecs_os_free(out);

    /* Without a bus mute still mutes everything */
    out = shell_audio_ok(world, s, "audio mute on");
    test_str(out, "mute on");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, AudioSettings)->mute, true);

    bool error = false;
    out = box_command_run(world, s, "audio mute nowhere", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_bypass(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t verb = ecs_lookup(world, "verb");
    test_assert(verb != 0);
    shell_audio_run(world, 2);

    char *out = shell_audio_ok(world, s, "audio bypass verb");
    test_str(out, "bypass verb on");
    ecs_os_free(out);
    test_assert(ecs_has_id(world, verb, EcsDisabled));

    out = shell_audio_ok(world, s, "audio");
    test_assert(strstr(out, "insert 0 verb: reverb") != NULL);
    test_assert(strstr(out, "(bypassed)") != NULL);
    ecs_os_free(out);

    out = shell_audio_ok(world, s, "audio bypass verb");
    test_str(out, "bypass verb off");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, verb, EcsDisabled));

    out = shell_audio_ok(world, s, "audio bypass verb on");
    ecs_os_free(out);
    test_assert(ecs_has_id(world, verb, EcsDisabled));
    out = shell_audio_ok(world, s, "audio bypass verb off");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, verb, EcsDisabled));

    bool error = false;
    out = box_command_run(world, s, "audio bypass hall", &error);
    test_bool(error, true);
    test_assert(strstr(out, "is not an effect") != NULL);
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, ecs_lookup(world, "hall"), EcsDisabled));
    out = box_command_run(world, s, "audio bypass nowhere", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Capture a click on the reverb bus of test/assets/audio/audition.flecs and get the peak of
 * its tail, from 0.3 seconds on.
 *
 * @param world The world
 * @param s The session
 * @return The peak of the left channel
 */
static int32_t shell_audio_tail(
    ecs_world_t *world,
    ecs_entity_t s)
{
    int32_t frames = 0, result;
    shell_audio_run(world, 10);
    char *name = shell_audio_capture(world, s, "1");
    test_bool(box_audio_play(world, ecs_lookup(world, "click"), 1.0f), true);
    int16_t *wav = shell_audio_capture_read(world, s, name, &frames);
    test_int(frames, SHELL_AUDIO_RATE);
    test_assert(shell_audio_peak(wav, 0, 0, SHELL_AUDIO_RATE / 10) > 1000);
    result = shell_audio_peak(wav, 0, SHELL_AUDIO_RATE * 3 / 10, frames);
    ecs_os_free(wav);

    return result;
}

void Shell_audio_bypass_capture(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    test_assert(shell_audio_tail(world, s) > 30);

    char *out = shell_audio_ok(world, s, "audio bypass verb");
    ecs_os_free(out);
    shell_audio_run(world, 120);
    test_assert(shell_audio_tail(world, s) < 5);

    out = shell_audio_ok(world, s, "audio bypass verb");
    ecs_os_free(out);
    test_assert(shell_audio_tail(world, s) > 30);
    ecs_fini(world);
}

void Shell_audio_note(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    shell_audio_run(world, 2);

    float pitch = 0;
    test_bool(box_cmd_audio_note_parse("C4", &pitch), true);
    test_flt(pitch, 60);
    test_bool(box_cmd_audio_note_parse("C2", &pitch), true);
    test_flt(pitch, 36);
    test_bool(box_cmd_audio_note_parse("F#3", &pitch), true);
    test_flt(pitch, 54);
    test_bool(box_cmd_audio_note_parse("Bb2", &pitch), true);
    test_flt(pitch, 46);
    test_bool(box_cmd_audio_note_parse("a4", &pitch), true);
    test_flt(pitch, 69);
    test_bool(box_cmd_audio_note_parse("C-1", &pitch), true);
    test_flt(pitch, 0);
    test_bool(box_cmd_audio_note_parse("72", &pitch), true);
    test_flt(pitch, 72);
    test_bool(box_cmd_audio_note_parse("H4", &pitch), false);
    test_bool(box_cmd_audio_note_parse("C", &pitch), false);
    test_bool(box_cmd_audio_note_parse("C4x", &pitch), false);
    test_bool(box_cmd_audio_note_parse("128", &pitch), false);
    test_bool(box_cmd_audio_note_parse("", &pitch), false);

    char *out = shell_audio_ok(world, s, "audio note lead A4");
    test_str(out, "note lead 69.00 (440.00 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio note lead 60 0.5 2");
    test_str(out, "note lead 60.00 (261.63 Hz) velocity 0.50 for 2.000 s");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio note lead F#3");
    test_str(out, "note lead 54.00 (185.00 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);

    /* A sound plays through an instrument of its own at the root of A4 */
    out = shell_audio_ok(world, s, "audio note tone_synth C5");
    test_str(out, "note tone_synth 72.00 (523.25 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);
    test_assert(ecs_lookup(world, "box.command.audition") != 0);
    test_assert(!ecs_has(world, ecs_lookup(world, "tone_synth"), Instrument));
    test_assert(ecs_get(world, ecs_lookup(world, "lead"), Instrument)->sound ==
        ecs_lookup(world, "tone_synth"));
    test_assert(ecs_singleton_get(world, Transport) == NULL ||
        !ecs_singleton_get(world, Transport)->playing);

    bool error = false;
    out = box_command_run(world, s, "audio note lead H4", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid note 'H4'") != NULL);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio note lead", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio note lead C4 2", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio note lead C4 1 0", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio note nowhere C4", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio note hall C4", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Count the rising zero crossings of the left channel of a capture over a range of frames.
 *
 * @param samples The samples, interleaved
 * @param from First frame
 * @param to Frame past the last
 * @return The count
 */
static int32_t shell_audio_crossings(
    const int16_t *samples,
    int32_t from,
    int32_t to)
{
    int32_t i, result = 0;
    for (i = from + 1; i < to; i ++) {
        result += samples[(i - 1) * 2] < 0 && samples[i * 2] >= 0;
    }

    return result;
}

void Shell_audio_note_capture(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    int32_t frames = 0;
    shell_audio_run(world, 5);

    /* A4 on an instrument at the root of A4 plays 440 Hz: 88 periods in 0.2 seconds */
    char *name = shell_audio_capture(world, s, "1");
    char *out = shell_audio_ok(world, s, "audio note lead A4");
    ecs_os_free(out);
    int16_t *wav = shell_audio_capture_read(world, s, name, &frames);
    test_int(frames, SHELL_AUDIO_RATE);
    int32_t from = SHELL_AUDIO_RATE / 10, to = SHELL_AUDIO_RATE * 3 / 10;
    test_assert(shell_audio_peak(wav, 0, from, to) > 5000);
    int32_t n = shell_audio_crossings(wav, from, to);
    test_assert(n >= 87 && n <= 89);
    /* Released after 0.5 seconds */
    test_assert(shell_audio_peak(wav, 0, SHELL_AUDIO_RATE * 7 / 10, frames) < 30);
    ecs_os_free(wav);

    /* A5 of the sound itself plays 880 Hz */
    name = shell_audio_capture(world, s, "0.5");
    out = shell_audio_ok(world, s, "audio note tone_synth A5");
    ecs_os_free(out);
    wav = shell_audio_capture_read(world, s, name, &frames);
    n = shell_audio_crossings(wav, from, to);
    test_assert(n >= 175 && n <= 177);
    ecs_os_free(wav);
    ecs_fini(world);
}
=======
void Shell_audio_notes_placed_twice(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio notes lead", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "notes of lead: pitched, instrument chords_inst, 4/4 at 120 bpm, "
        "bars 1..4, 6 notes;", 83) == 0);
    test_assert(strstr(out,
        "\nbar 1: 1.1.000 C5 1/4 v0.80, 1.2.000 E5 1/8 v0.60, 1.2.240 G5 1/8 v0.60"
        "\nbar 2: -"
        "\nbar 3: 3.1.000 C5 1/4 v0.80, 3.2.000 E5 1/8 v0.60, 3.2.240 G5 1/8 v0.60"
        "\nbar 4: -") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio notes lead bars=3..3", &error);
    test_bool(error, false);
    test_assert(strstr(out, "bars 3..3, 3 notes;") != NULL);
    test_assert(strstr(out, "\nbar 3: 3.1.000 C5 1/4 v0.80,") != NULL);
    test_assert(strstr(out, "bar 1:") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio notes duo", &error);
    test_bool(error, false);
    const char *bars = strstr(out, "\nbar 1:");
    test_str(bars, "\nbar 1: 1.1.000 C4 1/4 v1.00, 1.3.000 E4 1/8 v1.00\nbars 2..4: -");
    ecs_os_free(out);

    out = box_command_run(world, s, "audio notes lead", &error);
    char *again = box_command_run(world, s, "audio notes lead", &error);
    test_str(out, again);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_roll(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio notes duo roll bars=1", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "roll of duo: pitched", 20) == 0);
    test_assert(strstr(out, "\nE4 |........x=......|\nC4 |x===............|") != NULL);
    test_assert(strstr(out, "D4") == NULL);
    const char *rows = strstr(out, "\nE4 |");
    test_str(rows, "\nE4 |........x=......|\nC4 |x===............|");
    int32_t lines = 0;
    const char *ptr;
    for (ptr = out; *ptr; ptr ++) {
        lines += *ptr == '\n';
    }

    test_int(lines, 3);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio notes duo bars=1 roll", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nE4 |........x=......|\nC4 |x===............|") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_drums(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio notes pulse bars=1..2", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "notes of pulse: drums", 21) == 0);
    test_assert(strstr(out, "\nbar 1: 1.1.000 chords_kick_sound 1/16 v1.00, "
        "1.3.000 chords_kick_sound 1/16 v1.00") != NULL);
    test_assert(strstr(out, "\nbar 2: 2.1.000 chords_kick_sound 1/16 v1.00, "
        "2.3.000 chords_kick_sound 1/16 v1.00") != NULL);
    test_assert(strstr(out, "C1") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio notes pulse roll bars=1", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nchords_kick_sound |x.......x.......|") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_errors(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = false;
    char *out = box_command_run(world, s, "audio notes", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio notes nope", &error);
    test_bool(error, true);
    test_assert(strstr(out, "no track 'nope'") != NULL);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio notes lead bars=3..1", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio notes lead loud", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio chords nope", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_chords_progression(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio chords", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "chords of bars 1..4 from bass, duo, keys, lead; "
        "drums left out: pulse; a chord per bar (", 88) == 0);
    test_assert(strstr(out,
        "\n1.1 C 4 beats: C E G"
        "\n2.1 Am 4 beats: A C E"
        "\n3.1 F 4 beats: F A C"
        "\n4.1 G7 4 beats: G B D F"
        "\nkey of the bars: C major (r ") != NULL);
    test_assert(strstr(out, "\nkey of the song: C major (r ") != NULL);
    test_assert(strstr(out, "\noutside C major: none") != NULL);
    char *again = box_command_run(world, s, "audio chords", &error);
    test_str(out, again);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_chords_tracks_and_bars(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio chords keys bass bars=2..3", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "chords of bars 2..3 from keys, bass; a chord per bar", 52) == 0);
    test_assert(strstr(out, "\n2.1 Am 4 beats: A C E\n3.1 F 4 beats: F A C\nkey of the bars:")
        != NULL);
    test_assert(strstr(out, "1.1 C") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio chords keys bars=1", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\n1.1 C 4 beats: C E G\n") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio chords pulse", &error);
    test_bool(error, false);
    test_assert(strstr(out, "no pitched track; drums left out: pulse") != NULL);
    test_assert(strstr(out, "\n1.1 N.C. 16 beats") != NULL);
    test_assert(strstr(out, "\nkey of the bars: none") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}
>>>>>>> 1cde799b5b3139d3ab3a75b563906ec8f7299e0e
