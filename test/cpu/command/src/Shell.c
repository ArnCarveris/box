/**
 * @file Shell.c
 * @brief Tests for the box commands in the flecs script shell and box_command_run.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    test_assert(strstr(out, "audio [action] [value]  Print the audio status") != NULL);
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
