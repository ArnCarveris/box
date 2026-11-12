/**
 * @file Shell.c
 * @brief Tests for the box commands in the flecs script shell and box_command_run.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../../src/posix.h"
#include <math.h>
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
    test_assert(strstr(out, "\naudio  Print the audio state") != NULL);
    test_assert(strstr(out, "\naudio.plot <wav> [options...]  Draw a PNG resource") != NULL);
    test_assert(strstr(out, "\naudio.mute [state]  Set or toggle the mute") != NULL);
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
    reply = ECS_HTTP_REPLY_INIT;
    test_int(ecs_http_server_request(srv, "PUT", path, "key_press W 2", &reply), 0);
    test_int(reply.code, 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, "{\"results\":[{\"type\":\"flecs.meta.i32\", \"result\":3}], "
        "\"errors\":[]}");
    ecs_os_free(body);

    reply = ECS_HTTP_REPLY_INIT;
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

    out = box_command_run(world, s, "audio.volume 0.25", &error);
    test_bool(error, false);
    test_str(out, "master volume 0.25");
    ecs_os_free(out);
    test_flt(ecs_singleton_get(world, AudioSettings)->master_volume, 0.25f);

    out = box_command_run(world, s, "audio.mute on", &error);
    test_str(out, "mute on");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, AudioSettings)->mute, true);
    out = box_command_run(world, s, "audio.mute", &error);
    test_str(out, "mute off");
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.volume loud", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio sing", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Delta of a frame of the tests */
#define RENDER_DT (1.0f / 60.0f)

/** Frames per second of a render */
#define RENDER_RATE (48000)

/** Frames of the app a render may take at most in the tests */
#define RENDER_WAIT (3000)

/** Frames of the app of the test of the live output */
#define RENDER_LIVE_FRAMES (240)

/** Frames the tap of the live output keeps */
#define RENDER_TAP_MAX (RENDER_LIVE_FRAMES * 800)

/** The scene of the tests */
#define RENDER_SCENE "test/assets/audio/render.flecs"

/** The song of the tests */
#define RENDER_SONG "test/assets/audio/songs/render_stems.flecs"

/** A WAV that a render wrote, as floats */
typedef struct render_wav_t {
    float *left;            /**< Left channel */
    float *right;           /**< Right channel */
    int32_t frames;         /**< Frames */
    uint8_t *bytes;         /**< The file */
    long size;              /**< Bytes of the file */
} render_wav_t;

/** Output of the live world, left and right */
static float render_tap[RENDER_TAP_MAX * 2];

/** Frames in render_tap */
static int32_t render_tap_count;

static void render_tap_out(
    const float *left,
    const float *right,
    int32_t frames,
    void *ctx)
{
    int32_t i;
    (void)ctx;
    for (i = 0; i < frames && render_tap_count < RENDER_TAP_MAX; i ++) {
        render_tap[render_tap_count * 2] = left[i];
        render_tap[render_tap_count * 2 + 1] = right[i];
        render_tap_count ++;
    }
}

static ecs_world_t* render_world(
    bool output,
    bool scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    if (output) {
        render_tap_count = 0;
        box_audio_output_open(world, &(box_audio_output_desc_t){
            .headless = true, .output = render_tap_out });
        test_assert(box_audio_output_transport(world) != NULL);
    }

    if (scene) {
        char *path = box_app_locate(RENDER_SCENE);
        ecs_entity_t script = ecs_script(world, { .filename = path });
        ecs_os_free(path);
        test_assert(script != 0);
        const EcsScript *s = ecs_get(world, script, EcsScript);
        test_assert(s != NULL);
        test_assert(s->error == NULL);
    }

    return world;
}

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

    return world;
}

static void render_song(
    ecs_world_t *world)
{
    int32_t i;
    box_audio_song_load(world, RENDER_SONG);
    for (i = 0; i < 600; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
        const AudioSongStatus *st = ecs_singleton_get(world, AudioSongStatus);
        if (st && st->state != SongLoading) {
            break;
        }
    }

    test_int(ecs_singleton_get(world, AudioSongStatus)->state, SongReady);
}

static long render_size(
    const char *path)
{
    FILE *f = fopen(path, "rb");
    long result = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        result = ftell(f);
        fclose(f);
    }

    return result;
}

static int16_t render_i16(
    const uint8_t *p)
{
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

/** Run a render command and the frames of the app until its WAV is written, then read it.
 *
 * @param world The world
 * @param s The session
 * @param cmd The command line
 * @param frames Receives the frames of the app it took, may be NULL
 * @return The WAV
 */
static render_wav_t render_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    int32_t *frames)
{
    render_wav_t result = {0};
    ecs_shell_result_t r = ecs_shell_run(world, s, cmd);
    test_int(ecs_vec_count(&r.errors), 0);
    const ecs_value_t *v = box_command_value(&r);
    test_assert(v != NULL);
    test_uint(v->type, ecs_id(ecs_shell_resource_t));
    char *name = ecs_os_strdup(((const ecs_shell_resource_t*)v->ptr)->name);
    ecs_shell_result_fini(world, &r);
    test_assert(name != NULL && name[0]);
    char *path = box_command_resource_file(world, s, name);
    test_assert(path != NULL);
    int32_t i;
    for (i = 0; i < RENDER_WAIT && render_size(path) < 44; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
    }

    test_assert(i < RENDER_WAIT);
    if (frames) {
        *frames = i;
    }

    FILE *f = fopen(path, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    result.size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result.bytes = ecs_os_malloc((ecs_size_t)result.size);
    test_assert(fread(result.bytes, 1, (size_t)result.size, f) == (size_t)result.size);
    fclose(f);
    test_assert(!memcmp(result.bytes, "RIFF", 4));
    result.frames = (int32_t)((result.size - 44) / 4);
    result.left = ecs_os_malloc_n(float, result.frames + 1);
    result.right = ecs_os_malloc_n(float, result.frames + 1);
    for (i = 0; i < result.frames; i ++) {
        result.left[i] = (float)render_i16(result.bytes + 44 + i * 4) / 32768.0f;
        result.right[i] = (float)render_i16(result.bytes + 44 + i * 4 + 2) / 32768.0f;
    }

    ecs_os_free(path);
    ecs_os_free(name);

    return result;
}

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
    char *line = flecs_asprintf("audio.capture %s", seconds);
    ecs_shell_result_t r = ecs_shell_run(world, s, line);
    test_int(ecs_vec_count(&r.errors), 0);
    const ecs_value_t *v = box_command_value(&r);
    test_assert(v != NULL);
    test_uint(v->type, ecs_id(ecs_shell_resource_t));
    char *result = ecs_os_strdup(((const ecs_shell_resource_t*)v->ptr)->name);
    ecs_shell_result_fini(world, &r);
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

static void render_free(
    render_wav_t *wav)
{
    ecs_os_free(wav->left);
    ecs_os_free(wav->right);
    ecs_os_free(wav->bytes);
}

/** Amplitude of a frequency in a span of samples, a Goertzel filter.
 *
 * @param data The samples
 * @param count Number of samples
 * @param frequency Hz
 * @return The amplitude, 1 for a sine of peak 1
 */
static double render_tone(
    const float *data,
    int32_t count,
    double frequency)
{
    double w = 2.0 * 3.14159265358979323846 * frequency / RENDER_RATE;
    double c = 2.0 * cos(w), s0, s1 = 0, s2 = 0;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s0 = data[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return count ? 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / count : 0;
}

static float render_peak(
    const render_wav_t *wav)
{
    float result = 0;
    int32_t i;
    for (i = 0; i < wav->frames; i ++) {
        result = fabsf(wav->left[i]) > result ? fabsf(wav->left[i]) : result;
    }

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

void Shell_audio_render_sound(void) {
    ecs_world_t *world = render_world(false, true);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, RENDER_DT);

    render_wav_t a = render_run(world, s, "audio.render render_blip dur=0.5 tail=0.25", NULL);
    render_wav_t b = render_run(world, s, "audio.render render_blip dur=0.5 tail=0.25", NULL);
    test_int(a.frames, RENDER_RATE * 3 / 4);
    test_int(a.size, b.size);
    test_assert(!memcmp(a.bytes, b.bytes, (size_t)a.size));
    test_assert(render_peak(&a) > 0.1f);
    test_assert(render_tone(a.left + 4800, 9600, 440) > 0.05);

    render_wav_t c = render_run(world, s, "audio.render render_blip dur=0.2 tail=0.1", NULL);
    test_int(c.frames, RENDER_RATE * 3 / 10);

    render_wav_t d = render_run(world, s, "audio.render render_blip note=A5 dur=0.2 tail=0",
        NULL);
    test_int(d.frames, RENDER_RATE / 5);
    test_assert(render_tone(d.left + 2400, 4800, 880) > 0.05);
    test_assert(render_tone(d.left + 2400, 4800, 440) < 0.01);

    render_wav_t e = render_run(world, s, "audio.render render_blip dur=0.2", NULL);
    test_assert(e.frames > RENDER_RATE / 5);
    test_assert(e.frames < RENDER_RATE);

    render_wav_t f = render_run(world, s, "audio.render render_keys note=69 vel=0.5 dur=0.3 "
        "tail=0.2", NULL);
    test_int(f.frames, RENDER_RATE / 2);
    test_assert(render_peak(&f) > 0.01f);

    render_free(&a);
    render_free(&b);
    render_free(&c);
    render_free(&d);
    render_free(&e);
    render_free(&f);
    ecs_fini(world);
}

void Shell_audio_render_isolated(void) {
    static float with[RENDER_TAP_MAX * 2];
    int32_t i, frames = 0, count;

    ecs_world_t *world = render_world(true, true);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    for (i = 0; i < 20; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
    }

    render_wav_t wav = render_run(world, s, "audio.render render_blip dur=0.3 tail=0.2",
        &frames);
    test_assert(20 + frames < RENDER_LIVE_FRAMES);
    for (i = 20 + frames; i < RENDER_LIVE_FRAMES; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
    }

    test_int(wav.frames, RENDER_RATE / 2);
    test_assert(render_tone(wav.left + 2400, 9600, 440) > 0.05);
    test_assert(render_tone(wav.left + 2400, 9600, 1000) < 0.001);
    count = render_tap_count;
    test_assert(count > RENDER_RATE);
    memcpy(with, render_tap, (size_t)count * 2 * sizeof(float));
    render_free(&wav);
    ecs_fini(world);

    world = render_world(true, true);
    for (i = 0; i < RENDER_LIVE_FRAMES; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
    }

    test_int(render_tap_count, count);
    test_assert(!memcmp(with, render_tap, (size_t)count * 2 * sizeof(float)));
    ecs_fini(world);
}

void Shell_audio_render_stem(void) {
    ecs_world_t *world = render_world(false, false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    render_song(world);

    render_wav_t low = render_run(world, s, "audio.render render_low bars=1..2", NULL);
    render_wav_t high = render_run(world, s, "audio.render render_high bars=1..2", NULL);
    render_wav_t both = render_run(world, s, "audio.render song bars=1..2", NULL);
    render_wav_t muted = render_run(world, s, "audio.render song bars=1..2 mute=render_low",
        NULL);
    test_int(low.frames, RENDER_RATE * 4);
    test_int(high.frames, RENDER_RATE * 4);
    test_assert(render_tone(low.left + RENDER_RATE, RENDER_RATE, 220) > 0.05);
    test_assert(render_tone(low.left + RENDER_RATE, RENDER_RATE, 1760) < 0.001);
    test_assert(render_tone(high.left + RENDER_RATE, RENDER_RATE, 1760) > 0.05);
    test_assert(render_tone(high.left + RENDER_RATE, RENDER_RATE, 220) < 0.001);
    test_assert(render_tone(both.left + RENDER_RATE, RENDER_RATE, 220) > 0.05);
    test_assert(render_tone(both.left + RENDER_RATE, RENDER_RATE, 1760) > 0.05);
    test_assert(render_tone(muted.left + RENDER_RATE, RENDER_RATE, 220) < 0.001);
    test_assert(render_tone(muted.left + RENDER_RATE, RENDER_RATE, 1760) > 0.05);
    test_int(ecs_get(world, ecs_lookup(world, "render_low"), AudioBus)->mute, false);
    test_int(ecs_get(world, ecs_lookup(world, "render_low"), AudioBus)->solo, false);
    render_free(&low);
    render_free(&high);
    render_free(&both);
    render_free(&muted);
    ecs_fini(world);
}

void Shell_audio_render_section(void) {
    ecs_world_t *world = render_world(false, false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    render_song(world);
    const Transport before = *ecs_singleton_get(world, Transport);

    render_wav_t wav = render_run(world, s, "audio.render song bars=2..3", NULL);
    test_int(wav.frames, RENDER_RATE * 4);
    test_assert(render_peak(&wav) > 0.1f);

    render_wav_t beats = render_run(world, s, "audio.render song beats=4..6 tail=0.5", NULL);
    test_int(beats.frames, RENDER_RATE * 3 / 2);

    const Transport *after = ecs_singleton_get(world, Transport);
    test_bool(after->playing, before.playing);
    test_flt(after->position, before.position);
    test_bool(after->loop, before.loop);
    render_free(&wav);
    render_free(&beats);
    ecs_fini(world);
}

void Shell_audio_render_faster(void) {
    ecs_world_t *world = render_world(false, false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    render_song(world);

    ecs_time_t start;
    ecs_time_measure(&start);
    render_wav_t wav = render_run(world, s, "audio.render song bars=1..4", NULL);
    double seconds = ecs_time_measure(&start);
    test_int(wav.frames, RENDER_RATE * 8);
    test_assert(seconds < 8.0);
    render_free(&wav);
    ecs_fini(world);
}

void Shell_audio_render_capture(void) {
    ecs_world_t *world = render_world(true, false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    render_song(world);
    int32_t i, from = render_tap_count;
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->position = 0;
    tr->loop = false;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    for (i = 0; i < 150; i ++) {
        test_bool(ecs_progress(world, RENDER_DT), true);
    }

    tr = ecs_singleton_ensure(world, Transport);
    tr->playing = false;
    ecs_singleton_modified(world, Transport);
    ecs_progress(world, RENDER_DT);

    render_wav_t wav = render_run(world, s, "audio.render song bars=1..1", NULL);
    test_int(wav.frames, RENDER_RATE * 2);
    double diff = 0, sum = 0;
    for (i = 0; i < wav.frames; i ++) {
        double d = (double)wav.left[i] - (double)render_tap[(from + i) * 2];
        diff += d * d;
        sum += (double)wav.left[i] * (double)wav.left[i];
    }

    test_assert(sum > 0);
    test_assert(sqrt(diff / wav.frames) < 0.0001);
    render_free(&wav);
    ecs_fini(world);
}

void Shell_audio_render_errors(void) {
    ecs_world_t *world = render_world(false, true);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t made = ecs_entity(world, { .name = "render_c_bus" });
    ecs_ensure(world, made, AudioBus)->volume = 1;
    ecs_modified(world, made, AudioBus);
    ecs_progress(world, RENDER_DT);
    const char *lines[] = {
        "audio.render",
        "audio.render nothing_here",
        "audio.render render_drone",
        "audio.render render_blip bars=1..2",
        "audio.render render_blip note=H2",
        "audio.render render_blip vel=2",
        "audio.render render_blip dur=50 tail=20",
        "audio.render render_blip loud",
        "audio.render song",
        "audio.render render_c_bus dur=1",
        "audio.volume 1 note=C2"
    };

    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(lines) / sizeof(lines[0])); i ++) {
        bool error = false;
        char *out = box_command_run(world, s, lines[i], &error);
        test_bool(error, true);
        ecs_os_free(out);
    }

    bool error = true;
    char *out = box_command_run(world, s, "audio.render nothing_here", &error);
    test_assert(strstr(out, "not found") != NULL);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.render render_drone", &error);
    test_assert(strstr(out, "not a sound, an instrument or a bus") != NULL);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.render song", &error);
    test_assert(strstr(out, "no song is loaded") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_solo(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition_buses.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t left = ecs_lookup(world, "left");
    ecs_entity_t right = ecs_lookup(world, "right");
    test_assert(left != 0);
    test_assert(right != 0);

    char *out = shell_audio_ok(world, s, "audio.solo left");
    test_str(out, "solo left on");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, true);
    test_bool(ecs_get(world, right, AudioBus)->solo, false);

    out = shell_audio_ok(world, s, "audio");
    test_assert(strstr(out, "\n  left: group volume 1.00 pan -1.00 width 1.00 solo -> master")
        != NULL);
    ecs_os_free(out);

    out = shell_audio_ok(world, s, "audio.solo left");
    test_str(out, "solo left off");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, false);

    out = shell_audio_ok(world, s, "audio.solo right on");
    test_str(out, "solo right on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio.solo left on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio.solo none");
    test_str(out, "solo none, 2 cleared");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->solo, false);
    test_bool(ecs_get(world, right, AudioBus)->solo, false);

    bool error = false;
    out = box_command_run(world, s, "audio.solo nowhere", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.solo left_source", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.solo left maybe", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_solo_capture(void) {
    ecs_world_t *world = shell_audio_world("test/assets/audio/audition_buses.flecs");
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    int32_t frames = 0;
    shell_audio_run(world, 5);

    char *out = shell_audio_ok(world, s, "audio.solo left");
    ecs_os_free(out);
    shell_audio_run(world, 5);
    int16_t *wav = shell_audio_capture_read(world, s,
        shell_audio_capture(world, s, "0.25"), &frames);
    test_int(frames, SHELL_AUDIO_RATE / 4);
    test_assert(shell_audio_peak(wav, 0, 2400, frames) > 3000);
    test_assert(shell_audio_peak(wav, 1, 2400, frames) < 30);
    ecs_os_free(wav);

    out = shell_audio_ok(world, s, "audio.solo none");
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

    char *out = shell_audio_ok(world, s, "audio.mute_bus left");
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

    out = shell_audio_ok(world, s, "audio.mute_bus left off");
    test_str(out, "mute left off");
    ecs_os_free(out);
    test_bool(ecs_get(world, left, AudioBus)->mute, false);

    out = shell_audio_ok(world, s, "audio.mute_bus master on");
    test_str(out, "mute box.audio.master on");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio.mute_bus master off");
    ecs_os_free(out);

    /* Without a bus mute still mutes everything */
    out = shell_audio_ok(world, s, "audio.mute on");
    test_str(out, "mute on");
    ecs_os_free(out);
    test_bool(ecs_singleton_get(world, AudioSettings)->mute, true);

    bool error = false;
    out = box_command_run(world, s, "audio.mute_bus nowhere", &error);
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

    char *out = shell_audio_ok(world, s, "audio.bypass verb");
    test_str(out, "bypass verb on");
    ecs_os_free(out);
    test_assert(ecs_has_id(world, verb, EcsDisabled));

    out = shell_audio_ok(world, s, "audio");
    test_assert(strstr(out, "insert 0 verb: reverb") != NULL);
    test_assert(strstr(out, "(bypassed)") != NULL);
    ecs_os_free(out);

    out = shell_audio_ok(world, s, "audio.bypass verb");
    test_str(out, "bypass verb off");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, verb, EcsDisabled));

    out = shell_audio_ok(world, s, "audio.bypass verb on");
    ecs_os_free(out);
    test_assert(ecs_has_id(world, verb, EcsDisabled));
    out = shell_audio_ok(world, s, "audio.bypass verb off");
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, verb, EcsDisabled));

    bool error = false;
    out = box_command_run(world, s, "audio.bypass hall", &error);
    test_bool(error, true);
    test_assert(strstr(out, "is not an effect") != NULL);
    ecs_os_free(out);
    test_assert(!ecs_has_id(world, ecs_lookup(world, "hall"), EcsDisabled));
    out = box_command_run(world, s, "audio.bypass nowhere", &error);
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

    char *out = shell_audio_ok(world, s, "audio.bypass verb");
    ecs_os_free(out);
    shell_audio_run(world, 120);
    test_assert(shell_audio_tail(world, s) < 5);

    out = shell_audio_ok(world, s, "audio.bypass verb");
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

    char *out = shell_audio_ok(world, s, "audio.note lead A4");
    test_str(out, "note lead 69.00 (440.00 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio.note lead 60 0.5 2");
    test_str(out, "note lead 60.00 (261.63 Hz) velocity 0.50 for 2.000 s");
    ecs_os_free(out);
    out = shell_audio_ok(world, s, "audio.note lead F#3");
    test_str(out, "note lead 54.00 (185.00 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);

    /* A sound plays through an instrument of its own at the root of A4 */
    out = shell_audio_ok(world, s, "audio.note tone_synth C5");
    test_str(out, "note tone_synth 72.00 (523.25 Hz) velocity 1.00 for 0.500 s");
    ecs_os_free(out);
    test_assert(ecs_lookup(world, "box.command.audition") != 0);
    test_assert(!ecs_has(world, ecs_lookup(world, "tone_synth"), Instrument));
    test_assert(ecs_get(world, ecs_lookup(world, "lead"), Instrument)->sound ==
        ecs_lookup(world, "tone_synth"));
    test_assert(ecs_singleton_get(world, Transport) == NULL ||
        !ecs_singleton_get(world, Transport)->playing);

    bool error = false;
    out = box_command_run(world, s, "audio.note lead H4", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid note 'H4'") != NULL);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.note lead", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.note lead C4 2", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.note lead C4 1 0", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.note nowhere C4", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio.note hall C4", &error);
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
    char *out = shell_audio_ok(world, s, "audio.note lead A4");
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
    out = shell_audio_ok(world, s, "audio.note tone_synth A5");
    ecs_os_free(out);
    wav = shell_audio_capture_read(world, s, name, &frames);
    n = shell_audio_crossings(wav, from, to);
    test_assert(n >= 175 && n <= 177);
    ecs_os_free(wav);
    ecs_fini(world);
}

void Shell_audio_analyze(void) {
    // audio/analyze
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    bool error = true;
    char *out = box_command_run(world, s,
        "audio.analyze test/assets/audio/analyze_sine_1k.wav", &error);
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
        "audio.analyze test/assets/audio/analyze_sine_1k.wav", &error);
    test_bool(error, false);
    test_str(again, out);
    ecs_os_free(again);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.analyze test/assets/audio/analyze_sine_1k.wav 0.5 1",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, "2.000 s, span 0.500 to 1.000 s\n") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.analyze test/assets/audio/analyze_click.wav",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nclicks: start 0.500 (-6.0 dBFS), end ") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.analyze test/assets/audio/analyze_decay.wav",
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
    char *line = flecs_asprintf("audio.analyze %s", r.name);
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
    line = flecs_asprintf("audio.analyze %s", empty.name);
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
    char *out = box_command_run(world, s, "audio.analyze", &error);
    test_bool(error, true);
    test_assert(strstr(out, "missing argument 'wav' for command 'audio.analyze'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.analyze test/assets/audio/missing.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot find 'test/assets/audio/missing.wav'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.analyze test/assets/audio/analyze_adpcm.wav",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "unsupported WAV format") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.analyze test/assets/audio/corrupt.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "not a WAV file") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.analyze test/assets/audio/analyze_sine_1k.wav 3 4",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.analyze test/assets/audio/analyze_sine_1k.wav 1 0.5", &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.analyze test/assets/audio/analyze_sine_1k.wav soon", &error);
    test_bool(error, true);
    test_assert(strstr(out,
        "invalid argument 'soon' for parameter 'from' of command 'audio.analyze'") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Run audio.diff on two WAVs of test/assets/audio and check it succeeds */
static char* shell_diff(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *a,
    const char *b)
{
    char *line = flecs_asprintf("audio.diff test/assets/audio/%s test/assets/audio/%s", a, b);
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_assert(out != NULL);
    ecs_os_free(line);

    return out;
}

void Shell_audio_diff_identical(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    char *out = shell_diff(world, s, "diff_base.wav", "diff_base.wav");
    const char *head = "diff test/assets/audio/diff_base.wav test/assets/audio/diff_base.wav: "
        "48000 Hz, stereo, span 0.000 to 1.000 s\nidentical: yes, bit identical\n";
    test_assert(strncmp(out, head, strlen(head)) == 0);
    test_assert(strstr(out, "\nlength: a 1.000 s, b 1.000 s, b - a +0.000 s (+0 frames)\n")
        != NULL);
    test_assert(strstr(out, " LUFS (+0.0 dB), true peak ") != NULL);
    test_assert(strstr(out, "\nbands (b - a): sub +0.0 dB, low +0.0 dB") != NULL);
    test_assert(strstr(out, "\nbalance: unchanged") != NULL);
    test_assert(strstr(out, "\nstereo: correlation ") != NULL);
    test_assert(strstr(out, "\nnull test: offset +0 frames (+0.000 ms), residual -inf dB "
        "against a, -inf dBFS over 1.000 s\nresidual spans: none") != NULL);
    test_assert(strstr(out, "\nbars:") == NULL);

    char *again = shell_diff(world, s, "diff_base.wav", "diff_base.wav");
    test_str(again, out);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_diff_quieter(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    char *out = shell_diff(world, s, "diff_base.wav", "diff_quiet.wav");
    test_assert(strstr(out, "\nidentical: no, 96000 of 96000 samples differ, first at "
        "0.0000 s\n") != NULL);
    test_assert(strstr(out, " LUFS (-6.0 dB), true peak ") != NULL);
    test_assert(strstr(out, " dBFS (-6.0 dB)\nbands (b - a): sub -6.0 dB, low -6.0 dB, "
        "low-mid -6.0 dB, mid -6.0 dB, presence -6.0 dB") != NULL);
    test_assert(strstr(out, "\nbalance: unchanged") != NULL);
    test_assert(strstr(out, "\nnull test: offset +0 frames (+0.000 ms), residual -6.0 dB "
        "against a, ") != NULL);
    test_assert(strstr(out, "\nresidual spans: 0.000 to 1.000 s -26.0 dBFS "
        "(-6.0 dB against a)") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_diff_delay(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    char *out = shell_diff(world, s, "diff_base.wav", "diff_delay.wav");
    test_assert(strstr(out, "\nidentical: no, ") != NULL);
    test_assert(strstr(out, "\nnull test: offset +10 frames (+0.208 ms), residual -inf dB "
        "against a, -inf dBFS over 1.000 s\nresidual spans: none") != NULL);
    ecs_os_free(out);

    out = shell_diff(world, s, "diff_delay.wav", "diff_base.wav");
    test_assert(strstr(out, "\nnull test: offset -10 frames (-0.208 ms), residual -inf dB")
        != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_diff_boost(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    char *out = shell_diff(world, s, "diff_base.wav", "diff_boost.wav");
    test_assert(strstr(out, "\nbalance: presence changed most, +") != NULL);
    const char *presence = strstr(out, ", presence +");
    test_assert(presence != NULL);
    test_assert(atof(presence + strlen(", presence ")) > 2.0);
    test_assert(strstr(out, "\nnull test: offset +0 frames") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Write a mono float WAV of 8000 Hz, with a song chunk of 120 bpm and 4 beats per bar */
static char* shell_diff_song_wav(
    const float *data,
    int32_t frames,
    ecs_size_t *size)
{
    int32_t body = frames * 4, total = 12 + 24 + 24 + 8 + body;
    char *out = ecs_os_calloc(total);
    uint32_t u32;
    uint16_t u16;
    double start = 0;
    float tempo = 120, bar = 4;
    memcpy(out, "RIFF", 4);
    u32 = (uint32_t)(total - 8); memcpy(out + 4, &u32, 4);
    memcpy(out + 8, "WAVEfmt ", 8);
    u32 = 16; memcpy(out + 16, &u32, 4);
    u16 = 3; memcpy(out + 20, &u16, 2);
    u16 = 1; memcpy(out + 22, &u16, 2);
    u32 = 8000; memcpy(out + 24, &u32, 4);
    u32 = 32000; memcpy(out + 28, &u32, 4);
    u16 = 4; memcpy(out + 32, &u16, 2);
    u16 = 32; memcpy(out + 34, &u16, 2);
    memcpy(out + 36, "bxsg", 4);
    u32 = 16; memcpy(out + 40, &u32, 4);
    memcpy(out + 44, &start, 8);
    memcpy(out + 52, &tempo, 4);
    memcpy(out + 56, &bar, 4);
    memcpy(out + 60, "data", 4);
    u32 = (uint32_t)body; memcpy(out + 64, &u32, 4);
    memcpy(out + 68, data, (size_t)body);
    *size = total;

    return out;
}

void Shell_audio_diff_bars(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    int32_t i, frames = 48000;
    float *a = ecs_os_malloc_n(float, frames), *b = ecs_os_malloc_n(float, frames);
    for (i = 0; i < frames; i ++) {
        a[i] = 0.25f * sinf(2.0f * 3.14159265f * 440.0f * (float)i / 8000.0f);
        b[i] = a[i];
        if (i >= 16000 && i < 32000) {
            b[i] += 0.25f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 8000.0f);
        }
    }

    ecs_size_t size_a, size_b;
    char *wav_a = shell_diff_song_wav(a, frames, &size_a);
    char *wav_b = shell_diff_song_wav(b, frames, &size_b);
    ecs_shell_resource_t ra = {0}, rb = {0};
    test_int(ecs_shell_resource_create(world, s, "audio/wav", wav_a, size_a, &ra), 0);
    test_int(ecs_shell_resource_create(world, s, "audio/wav", wav_b, size_b, &rb), 0);

    char *line = flecs_asprintf("audio.diff %s %s", ra.name, rb.name);
    bool error = true;
    char *out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_assert(strstr(out, ": 8000 Hz, mono, span 0.000 to 6.000 s\n") != NULL);
    test_assert(strstr(out, "\nnull test: offset +0 frames (+0.000 ms), residual -4.8 dB "
        "against a, ") != NULL);
    test_assert(strstr(out, "\nresidual spans: 2.000 to 4.000 s ") != NULL);
    test_assert(strstr(out, ", mid +70.4 dB, presence quiet,") != NULL);
    test_assert(strstr(out, "\nbars: 120.0 bpm, 4.00 beats per bar, 1 of 3 bars differ\n"
        "bar 2: residual ") != NULL);
    test_assert(strstr(out, "\nbar 1") == NULL);
    test_assert(strstr(out, "\nbar 3") == NULL);
    ecs_os_free(out);
    ecs_os_free(line);

    line = flecs_asprintf("audio.diff %s %s 2 4", ra.name, rb.name);
    out = box_command_run(world, s, line, &error);
    test_bool(error, false);
    test_assert(strstr(out, "span 2.000 to 4.000 s\n") != NULL);
    test_assert(strstr(out, "1 of 1 bars differ\nbar 2: ") != NULL);
    ecs_os_free(out);
    ecs_os_free(line);

    ecs_os_free(ra.name);
    ecs_os_free(ra.content_type);
    ecs_os_free(rb.name);
    ecs_os_free(rb.content_type);
    ecs_os_free(wav_a);
    ecs_os_free(wav_b);
    ecs_os_free(a);
    ecs_os_free(b);
    ecs_fini(world);
}

void Shell_audio_diff_errors(void) {
    // audio/diff
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    bool error = false;
    char *out = box_command_run(world, s, "audio.diff test/assets/audio/diff_base.wav",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "missing argument 'b' for command 'audio.diff'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/missing.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot find 'test/assets/audio/missing.wav'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/corrupt.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot diff 'test/assets/audio/corrupt.wav': not a WAV file")
        != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/tone_44100.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "at 48000 Hz with 'test/assets/audio/tone_44100.wav' at 44100 Hz: "
        "the sample rates must be the same") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/diff_base.wav 2 3",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/diff_base.wav soon",
        &error);
    test_bool(error, true);
    test_assert(strstr(out,
        "invalid argument 'soon' for parameter 'from' of command 'audio.diff'") != NULL);
    ecs_os_free(out);

    error = true;
    out = box_command_run(world, s,
        "audio.diff test/assets/audio/diff_base.wav test/assets/audio/analyze_sine_1k.wav",
        &error);
    test_bool(error, false);
    test_assert(strstr(out, ": 48000 Hz, mono sums of a mono and a stereo WAV, span 0.000 to "
        "1.000 s\nidentical: no, one is mono and the other stereo\n") != NULL);
    test_assert(strstr(out, "\nlength: a 1.000 s, b 2.000 s, b - a +1.000 s (+48000 frames)")
        != NULL);
    test_assert(strstr(out, "\nstereo: mono, not compared") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** Read a PNG resource of a session and check its content type and signature.
 *
 * @param world The world
 * @param s The session
 * @param name The name of the resource
 * @param size Receives the bytes
 * @return The bytes, which the caller frees
 */
static uint8_t* shell_png_read(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *name,
    ecs_size_t *size)
{
    const char *type = NULL;
    uint8_t *bytes = ecs_shell_resource_read(world, s, name, size, &type);
    test_assert(bytes != NULL);
    test_assert(*size > 8);
    test_str(type, "image/png");
    test_assert(!memcmp(bytes, "\x89PNG\r\n\x1a\n", 8));

    return bytes;
}

void Shell_audio_plot(void) {
    // audio/plot
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    const char *lines[] = {
        "audio.plot test/assets/audio/analyze_sine_1k.wav",
        "audio.plot test/assets/audio/analyze_sine_1k.wav spectrogram",
        "audio.plot test/assets/audio/analyze_sine_1k.wav waveform size=640x320 from=0.5 to=1",
        "audio.plot test/assets/audio/analyze_inverted.wav spectrum min_db=-80",
        "audio.plot test/assets/audio/analyze_decay.wav loudness",
        "audio.plot test/assets/audio/analyze_click.wav spectrogram size=300x200 min_db=-120"
    };

    char *names[6];
    int32_t i;
    for (i = 0; i < 6; i ++) {
        ecs_shell_result_t r = ecs_shell_run(world, s, lines[i]);
        test_int(ecs_vec_count(&r.errors), 0);
        const ecs_value_t *v = box_command_value(&r);
        test_assert(v != NULL);
        test_uint(v->type, ecs_id(ecs_shell_resource_t));
        test_str(((const ecs_shell_resource_t*)v->ptr)->content_type, "image/png");
        names[i] = ecs_os_strdup(((const ecs_shell_resource_t*)v->ptr)->name);
        test_assert(names[i] != NULL);
        ecs_shell_result_fini(world, &r);
    }

    char *a = names[0], *b = names[1];
    test_assert(strcmp(a, b) != 0);

    ecs_size_t size_a = 0, size_b = 0;
    uint8_t *png_a = shell_png_read(world, s, a, &size_a);
    uint8_t *png_b = shell_png_read(world, s, b, &size_b);
    test_int(size_a, size_b);
    test_assert(!memcmp(png_a, png_b, (size_t)size_a));
    ecs_os_free(png_a);
    ecs_os_free(png_b);
    ecs_os_free(a);
    ecs_os_free(b);

    for (i = 2; i < 6; i ++) {
        ecs_size_t size = 0;
        uint8_t *png = shell_png_read(world, s, names[i], &size);
        ecs_os_free(png);
        ecs_os_free(names[i]);
    }

    ecs_fini(world);
}

void Shell_audio_plot_errors(void) {
    // audio/plot
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_progress(world, 0.016f);

    bool error = false;
    char *out = box_command_run(world, s, "audio.plot", &error);
    test_bool(error, true);
    test_assert(strstr(out, "missing argument 'wav' for command 'audio.plot'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.plot test/assets/audio/missing.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot find 'test/assets/audio/missing.wav'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.plot missing_resource waveform", &error);
    test_bool(error, true);
    test_assert(strstr(out, "cannot find 'missing_resource'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.plot test/assets/audio/corrupt.wav", &error);
    test_bool(error, true);
    test_assert(strstr(out, "not a WAV file") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s, "audio.plot test/assets/audio/analyze_sine_1k.wav pie",
        &error);
    test_bool(error, true);
    test_assert(strstr(out, "unknown plot argument 'pie'") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.plot test/assets/audio/analyze_sine_1k.wav size=10x10", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid size") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.plot test/assets/audio/analyze_sine_1k.wav min_db=6", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid min_db") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.plot test/assets/audio/analyze_sine_1k.wav from=3 to=4", &error);
    test_bool(error, true);
    test_assert(strstr(out, "empty span") != NULL);
    ecs_os_free(out);

    error = false;
    out = box_command_run(world, s,
        "audio.plot test/assets/audio/analyze_sine_1k.wav from=soon", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid from") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

/** A world with the song of test/assets/audio/chords.flecs, no output and no device */
void Shell_audio_meter_errors(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t music = ecs_entity(world, { .name = "music" });
    ecs_ensure(world, music, AudioBus)->volume = 0.5f;
    ecs_modified(world, music, AudioBus);
    ecs_progress(world, 0.016f);

    bool error = false;
    char *out = box_command_run(world, s, "audio.meter music 1", &error);
    test_bool(error, true);
    test_assert(strstr(out, "the output does not run") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter nothing 1", &error);
    test_bool(error, true);
    test_assert(strstr(out, "no bus 'nothing'") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter 0", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid seconds '0'") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter 1000", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter bars=2..1", &error);
    test_bool(error, true);
    test_assert(strstr(out, "invalid bars '2..1'") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter bars=1..2", &error);
    test_bool(error, true);
    test_assert(strstr(out, "no song is loaded") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter 2 bars=1..2", &error);
    test_bool(error, true);
    test_assert(strstr(out, "either seconds or bars=a..b") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.meter", &error);
    test_bool(error, true);
    ecs_os_free(out);
    test_assert(!ecs_has(world, music, AudioMeter));
    ecs_fini(world);
}

static ecs_world_t* shell_chords_world(
    ecs_entity_t *session)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *session = ecs_shell_session_init(world, NULL);
    char *path = box_app_locate("test/assets/audio/chords.flecs");
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);

    return world;
}

void Shell_audio_notes_placed_twice(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio.notes lead", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "notes of lead: pitched, instrument chords_inst, 4/4 at 120 bpm, "
        "bars 1..4, 6 notes;", 83) == 0);
    test_assert(strstr(out,
        "\nbar 1: 1.1.000 C5 1/4 v0.80, 1.2.000 E5 1/8 v0.60, 1.2.240 G5 1/8 v0.60"
        "\nbar 2: -"
        "\nbar 3: 3.1.000 C5 1/4 v0.80, 3.2.000 E5 1/8 v0.60, 3.2.240 G5 1/8 v0.60"
        "\nbar 4: -") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.notes lead bars=3..3", &error);
    test_bool(error, false);
    test_assert(strstr(out, "bars 3..3, 3 notes;") != NULL);
    test_assert(strstr(out, "\nbar 3: 3.1.000 C5 1/4 v0.80,") != NULL);
    test_assert(strstr(out, "bar 1:") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.notes duo", &error);
    test_bool(error, false);
    const char *bars = strstr(out, "\nbar 1:");
    test_str(bars, "\nbar 1: 1.1.000 C4 1/4 v1.00, 1.3.000 E4 1/8 v1.00\nbars 2..4: -");
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.notes lead", &error);
    char *again = box_command_run(world, s, "audio.notes lead", &error);
    test_str(out, again);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_roll(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio.notes duo roll bars=1", &error);
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

    out = box_command_run(world, s, "audio.notes duo bars=1 roll", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nE4 |........x=......|\nC4 |x===............|") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_drums(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio.notes pulse bars=1..2", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "notes of pulse: drums", 21) == 0);
    test_assert(strstr(out, "\nbar 1: 1.1.000 chords_kick_sound 1/16 v1.00, "
        "1.3.000 chords_kick_sound 1/16 v1.00") != NULL);
    test_assert(strstr(out, "\nbar 2: 2.1.000 chords_kick_sound 1/16 v1.00, "
        "2.3.000 chords_kick_sound 1/16 v1.00") != NULL);
    test_assert(strstr(out, "C1") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.notes pulse roll bars=1", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nchords_kick_sound |x.......x.......|") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_notes_errors(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = false;
    char *out = box_command_run(world, s, "audio.notes", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio.notes nope", &error);
    test_bool(error, true);
    test_assert(strstr(out, "no track 'nope'") != NULL);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio.notes lead bars=3..1", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio.notes lead loud", &error);
    test_bool(error, true);
    ecs_os_free(out);
    error = false;
    out = box_command_run(world, s, "audio.chords nope", &error);
    test_bool(error, true);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_chords_progression(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio.chords", &error);
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
    char *again = box_command_run(world, s, "audio.chords", &error);
    test_str(out, again);
    ecs_os_free(again);
    ecs_os_free(out);
    ecs_fini(world);
}

void Shell_audio_chords_tracks_and_bars(void) {
    ecs_entity_t s;
    ecs_world_t *world = shell_chords_world(&s);
    bool error = true;
    char *out = box_command_run(world, s, "audio.chords keys bass bars=2..3", &error);
    test_bool(error, false);
    test_assert(strncmp(out, "chords of bars 2..3 from keys, bass; a chord per bar", 52) == 0);
    test_assert(strstr(out, "\n2.1 Am 4 beats: A C E\n3.1 F 4 beats: F A C\nkey of the bars:")
        != NULL);
    test_assert(strstr(out, "1.1 C") == NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.chords keys bars=1", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\n1.1 C 4 beats: C E G\n") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio.chords pulse", &error);
    test_bool(error, false);
    test_assert(strstr(out, "no pitched track; drums left out: pulse") != NULL);
    test_assert(strstr(out, "\n1.1 N.C. 16 beats") != NULL);
    test_assert(strstr(out, "\nkey of the bars: none") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}
