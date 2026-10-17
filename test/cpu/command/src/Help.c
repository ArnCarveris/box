/**
 * @file Help.c
 * @brief Tests for the help command.
 */

#include <box.h>
#include <bake_test.h>
#include <command.h>
#include <string.h>

/** Command that prints nothing.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the arguments
 * @param result Receives the output
 */
static void Help_noop(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    *(char**)result->ptr = ecs_os_strdup("");
}

/** Add the dotted names of the commands under an entity of the command scope.
 *
 * @param world The world
 * @param scope The command scope
 * @param parent The entity whose children are visited
 * @param out The char* list
 */
static void Help_collect(
    ecs_world_t *world,
    ecs_entity_t scope,
    ecs_entity_t parent,
    ecs_vec_t *out)
{
    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (ecs_has(world, e, CommandInfo) && ecs_has(world, e, EcsScriptFunction)) {
                *ecs_vec_append_t(NULL, out, char*) =
                    ecs_get_path_w_sep(world, scope, e, ".", NULL);
            }

            Help_collect(world, scope, e, out);
        }
    }
}

/** Get the dotted names of every command in box.command.commands.
 *
 * @param world The world
 * @param out Receives the char* list, freed with Help_fini
 */
static void Help_names(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    ecs_entity_t scope = ecs_lookup(world, "box.command.commands");
    ecs_vec_init_t(NULL, out, char*, 0);
    Help_collect(world, scope, scope, out);
}

/** Free a list of Help_names.
 *
 * @param names The list
 */
static void Help_fini(
    ecs_vec_t *names)
{
    char **n = ecs_vec_first(names);
    for (int32_t i = 0; i < ecs_vec_count(names); i ++) {
        ecs_os_free(n[i]);
    }

    ecs_vec_fini_t(NULL, names, char*);
}

/** Copy the first characters of a string.
 *
 * @param str The string
 * @param len Characters to copy
 * @return The copy, which the caller frees
 */
static char* Help_strndup(
    const char *str,
    int32_t len)
{
    char *result = ecs_os_malloc(len + 1);
    ecs_os_memcpy(result, str, len);
    result[len] = '\0';

    return result;
}

/** Test whether an output has a line that starts with a command name.
 *
 * @param out The output
 * @param name The command name
 * @return Whether a line is the name alone or the name followed by spaces and the brief
 */
static bool Help_has_line(
    const char *out,
    const char *name)
{
    int32_t len = ecs_os_strlen(name);
    const char *line = out;
    bool result = false;
    while (line && !result) {
        result = !strncmp(line, name, len) &&
            (line[len] == '\n' || line[len] == '\0' || line[len] == ' ');
        line = strchr(line, '\n');
        line = line ? line + 1 : NULL;
    }

    return result;
}

void Help_list_all_commands(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "help", &error);
    test_bool(error, false);

    ecs_vec_t names;
    Help_names(world, &names);
    test_assert(ecs_vec_count(&names) > 40);
    char **n = ecs_vec_first(&names);
    for (int32_t i = 0; i < ecs_vec_count(&names); i ++) {
        test_assert(Help_has_line(out, n[i]));
    }

    test_assert(Help_has_line(out, "help"));
    test_assert(Help_has_line(out, "quit"));
    test_assert(Help_has_line(out, "capture"));
    test_assert(Help_has_line(out, "fps"));
    test_assert(Help_has_line(out, "mouse.click"));
    test_assert(strstr(out, "List the commands or show how to use one") != NULL);

    int32_t lines = 0;
    for (const char *p = out; *p; p ++) {
        lines += p[0] != '\n' && (p == out || p[-1] == '\n');
    }

    test_int(lines, ecs_vec_count(&names));

    Help_fini(&names);
    ecs_os_free(out);
    ecs_fini(world);
}

void Help_list_registered_after_import(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "zz.extra",
        .brief = "Do nothing",
        .callback = Help_noop
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "aaa",
        .brief = "Do nothing first",
        .callback = Help_noop
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "help", NULL);
    test_assert(!strncmp(out, "aaa ", 4));
    test_assert(strstr(out, "\n\nzz.extra ") != NULL);
    const char *last = strrchr(out, '\n');
    test_assert(!strncmp(last + 1, "zz.extra", 8));
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help zz.extra", NULL);
    test_str(out, "usage: zz.extra\nDo nothing");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_list_sorted_grouped(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "help", NULL);
    char *prev = NULL, *prev_group = NULL;
    int32_t blocks = 1, column = -1;
    bool new_block = false;
    char *line = out;
    while (line) {
        char *end = strchr(line, '\n');
        if (end) {
            end[0] = '\0';
        }

        if (!line[0]) {
            new_block = true;
        } else {
            char *space = strchr(line, ' ');
            int32_t len = space ? (int32_t)(space - line) : ecs_os_strlen(line);
            char *name = Help_strndup(line, len);
            char *dot = strchr(name, '.');
            char *group = dot ? Help_strndup(name, (int32_t)(dot - name)) : NULL;
            if (space) {
                int32_t col = (int32_t)(space - line) + (int32_t)strspn(space, " ");
                test_assert(col >= len + 2);
                test_assert(column == -1 || column == col);
                column = col;
            }

            if (new_block) {
                blocks ++;
                test_assert(group != NULL);
                test_assert(prev_group == NULL || strcmp(prev_group, group) < 0);
            } else if (prev) {
                test_assert(strcmp(prev, name) < 0);
                test_assert((group == NULL) == (prev_group == NULL));
                test_assert(!group || !strcmp(group, prev_group));
            } else {
                test_assert(group == NULL);
            }

            ecs_os_free(prev);
            ecs_os_free(prev_group);
            prev = name;
            prev_group = group;
            new_block = false;
        }

        line = end ? end + 1 : NULL;
    }

    test_assert(column > 0);
    test_assert(blocks >= 6);
    ecs_os_free(prev);
    ecs_os_free(prev_group);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help", NULL);
    char *key = strstr(out, "\n\nkey.down ");
    char *mouse = strstr(out, "\n\nmouse.button ");
    char *action = strstr(out, "\n\naction.down ");
    test_assert(key != NULL && mouse != NULL && action != NULL);
    test_assert(action < key && key < mouse);
    test_assert(strstr(out, "\n\nquit") == NULL);
    test_assert(strstr(out, "cd ") < strstr(out, "ls "));
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_usage_get(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "help get", &error);
    test_bool(error, false);
    test_str(out, "usage: get <entity> [component]\n"
        "Print a component or member value, every component when omitted\n"
        "  <entity>     string\n"
        "  [component]  string");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help mouse.scroll", NULL);
    test_str(out, "usage: mouse.scroll <dx> <dy>\n"
        "Move the wheel by dx, dy\n"
        "  <dx>  f64\n"
        "  <dy>  f64");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help help", NULL);
    test_str(out, "usage: help [command]\n"
        "List the commands or show how to use one\n"
        "  [command]  string");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_usage_optional_rest(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "typed",
        .brief = "Take one argument of each type",
        .params = {
            { .name = "n", .type = ecs_id(ecs_i32_t) },
            { .name = "x", .type = ecs_id(ecs_f64_t) },
            { .name = "on", .type = ecs_id(ecs_bool_t) },
            { .name = "e", .type = ecs_id(ecs_entity_t), .optional = true },
            { .name = "text", .type = ecs_id(ecs_string_t), .optional = true, .rest = true }
        },
        .callback = Help_noop
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "help typed", NULL);
    test_str(out, "usage: typed <n> <x> <on> [e] [text...]\n"
        "Take one argument of each type\n"
        "  <n>        i32\n"
        "  <x>        f64\n"
        "  <on>       bool\n"
        "  [e]        entity\n"
        "  [text...]  string");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help set", NULL);
    test_str(out, "usage: set <entity> <component> <value...>\n"
        "Assign a flecs script value to a component or member like Position.x\n"
        "  <entity>     string\n"
        "  <component>  string\n"
        "  <value...>   string");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help query", NULL);
    test_assert(!strncmp(out, "usage: query [name] [expr...]\n", 30));
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_unknown_suggestions(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "help quti", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'quti', did you mean: quit");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help mous", &error);
    test_bool(error, true);
    test_str(out,
        "error: unknown command 'mous', did you mean: mouse.button, mouse.click, mouse.drag");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help mouse.clik", &error);
    test_bool(error, true);
    test_assert(!strncmp(out,
        "error: unknown command 'mouse.clik', did you mean: mouse.click", 62));
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help scroll", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'scroll', did you mean: mouse.scroll");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_unknown_no_suggestions(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "help zzzzzzzz", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'zzzzzzzz'");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_group_mouse(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_vec_t names;
    int32_t mouse = 0;
    Help_names(world, &names);
    char **n = ecs_vec_first(&names);
    for (int32_t i = 0; i < ecs_vec_count(&names); i ++) {
        mouse += !strncmp(n[i], "mouse.", 6);
    }

    test_int(mouse, 7);

    ecs_entity_t s = box_terminal_new(world);
    bool error = true;
    char *out = box_terminal_run(world, s, "help mouse", &error);
    test_bool(error, false);
    int32_t lines = 0;
    const char *line = out;
    while (line) {
        test_assert(!strncmp(line, "mouse.", 6));
        lines ++;
        line = strchr(line, '\n');
        line = line ? line + 1 : NULL;
    }

    test_int(lines, mouse);
    test_assert(Help_has_line(out, "mouse.click"));
    test_assert(strstr(out, "Move the wheel by dx, dy") != NULL);

    char *dotted = box_terminal_run(world, s, "help mouse.", &error);
    test_bool(error, false);
    test_str(dotted, out);
    ecs_os_free(dotted);
    ecs_os_free(out);

    Help_fini(&names);
    ecs_fini(world);
}

void Help_no_brief_listed(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_command_register(world, &(box_command_desc_t){
        .name = "nobrief",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t) }},
        .callback = Help_noop
    });

    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, "help", NULL);
    test_assert(strstr(out, "\nnobrief\n") != NULL);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "help nobrief", NULL);
    test_str(out, "usage: nobrief <path>\n  <path>  string");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_every_command_has_brief(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t scope = ecs_lookup(world, "box.command.commands");
    ecs_vec_t names;
    Help_names(world, &names);
    char **n = ecs_vec_first(&names);
    for (int32_t i = 0; i < ecs_vec_count(&names); i ++) {
        ecs_entity_t cmd = ecs_lookup_path_w_sep(world, scope, n[i], ".", NULL, false);
        const char *brief = ecs_doc_get_brief(world, cmd);
        test_assert(brief != NULL);
        test_assert(brief[0] >= 'A' && brief[0] <= 'Z');
        test_assert(strchr(brief, '\n') == NULL);
        test_assert(brief[ecs_os_strlen(brief) - 1] != '.');
        test_assert(ecs_os_strlen(brief) <= 80);
    }

    Help_fini(&names);
    ecs_fini(world);
}

void Help_dispatch_error_mentions_help(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "nope 1 2", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'nope', see help");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse", &error);
    test_bool(error, true);
    test_str(out, "error: unknown command 'mouse', see help");
    ecs_os_free(out);

    ecs_fini(world);
}

void Help_quiet(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "help", NULL));
    ecs_os_free(box_terminal_run(world, s, "help get", NULL));
    ecs_os_free(box_terminal_run(world, s, "help quti", NULL));

    char *out = box_terminal_run(world, s, "commands", NULL);
    test_str(out, "$ help\n$ help get\n$ help quti\n"
        "error: unknown command 'quti', did you mean: quit");
    ecs_os_free(out);

    test_int(ecs_get(world, s, TerminalStatus)->commands, 4);

    ecs_fini(world);
}

void Help_http_exec(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *expect = box_terminal_run(world, s, "help", NULL);
    char *path = flecs_asprintf("/terminal/exec?session=%s&cmd=help", ecs_get_name(world, s));
    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    char *body = ecs_strbuf_get(&reply.body);
    test_str(body, expect);
    ecs_os_free(body);
    ecs_os_free(path);
    ecs_os_free(expect);

    expect = box_terminal_run(world, s, "help get", NULL);
    path = flecs_asprintf("/terminal/exec?session=%s&cmd=help%%20get", ecs_get_name(world, s));
    reply = (ecs_http_reply_t)ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", path, NULL, &reply), 200);
    body = ecs_strbuf_get(&reply.body);
    test_str(body, expect);
    ecs_os_free(body);
    ecs_os_free(path);
    ecs_os_free(expect);

    ecs_fini(world);
}

void Help_no_color_codes(void) {
    // command/help
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    const char *lines[] = { "help", "help get", "help mouse", "help quti", "help zzzzzzzz" };
    ecs_entity_t s = box_terminal_new(world);
    for (int32_t i = 0; i < 5; i ++) {
        char *out = box_terminal_run(world, s, lines[i], NULL);
        test_assert(strchr(out, '\033') == NULL);
        test_assert(strstr(out, "#[") == NULL);
        ecs_os_free(out);
    }

    ecs_fini(world);
}
