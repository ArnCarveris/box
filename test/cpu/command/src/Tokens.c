/**
 * @file Tokens.c
 * @brief Tests for the token ranges of command lines that the console colors.
 */

#include <box.h>
#include <bake_test.h>
#include <command.h>

/** Most tokens a test line has */
#define TOKENS_MAX (32)

/** Check the range, quoting and text of a token.
 *
 * @param line The command line
 * @param t The token
 * @param start Expected first byte
 * @param end Expected byte after the token
 * @param quoted Expected quoting
 * @param text Expected text as the dispatcher passes it, read without a world
 */
static void tokens_expect(
    const char *line,
    const box_command_token_t *t,
    int32_t start,
    int32_t end,
    bool quoted,
    const char *text)
{
    ecs_os_set_api_defaults();
    char *str = box_command_token_text(line, t);
    test_int(t->start, start);
    test_int(t->end, end);
    test_bool(t->quoted, quoted);
    test_str(str, text);
    ecs_os_free(str);
}

/** Command that prints the arguments it got, separated by |, skipping omitted ones.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the arguments
 * @param result Receives the output
 */
static void tokens_echo(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;
    (void)ctx;
    for (i = 1; i < argc; i ++) {
        const char *str = *(char**)argv[i].ptr;
        if (str) {
            ecs_strbuf_appendstr(&buf, i > 1 ? "|" : "");
            ecs_strbuf_appendstr(&buf, str);
        }
    }

    char *out = ecs_strbuf_get(&buf);
    *(char**)result->ptr = out ? out : ecs_os_strdup("");
}

void Tokens_words(void) {
    // command/tokens
    const char *line = "get things.alpha Position";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 3);
    tokens_expect(line, &t[0], 0, 3, false, "get");
    tokens_expect(line, &t[1], 4, 16, false, "things.alpha");
    tokens_expect(line, &t[2], 17, 25, false, "Position");
}

void Tokens_whitespace(void) {
    // command/tokens
    const char *line = "  ls \t a\r\nb  ";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 3);
    tokens_expect(line, &t[0], 2, 4, false, "ls");
    tokens_expect(line, &t[1], 7, 8, false, "a");
    tokens_expect(line, &t[2], 10, 11, false, "b");
}

void Tokens_empty_line(void) {
    // command/tokens
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens("", t, TOKENS_MAX), 0);
    test_int(box_command_tokens(" \t \n", t, TOKENS_MAX), 0);
    test_int(box_command_tokens("a b", NULL, 0), 2);
}

void Tokens_quoted(void) {
    // command/tokens
    const char *line = "key.type \"hello world\" 3";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 3);
    tokens_expect(line, &t[0], 0, 8, false, "key.type");
    tokens_expect(line, &t[1], 9, 22, true, "hello world");
    tokens_expect(line, &t[2], 23, 24, false, "3");

    const char *empty = "echo \"\" x";
    test_int(box_command_tokens(empty, t, TOKENS_MAX), 3);
    tokens_expect(empty, &t[1], 5, 7, true, "");
    tokens_expect(empty, &t[2], 8, 9, false, "x");
}

void Tokens_quoted_escape(void) {
    // command/tokens
    const char *line = "echo \"a \\\"b\\\" \\\\ c\" d";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 3);
    tokens_expect(line, &t[1], 5, 19, true, "a \"b\" \\ c");
    tokens_expect(line, &t[2], 20, 21, false, "d");
}

void Tokens_unterminated_quote(void) {
    // command/tokens
    const char *line = "echo \"open ended  {x";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 2);
    tokens_expect(line, &t[0], 0, 4, false, "echo");
    tokens_expect(line, &t[1], 5, 20, true, "open ended  {x");
}

void Tokens_unterminated_escape(void) {
    // command/tokens
    const char *line = "echo \"ab\\";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 2);
    tokens_expect(line, &t[1], 5, 9, true, "ab\\");

    const char *escaped = "echo \"ab\\\"";
    test_int(box_command_tokens(escaped, t, TOKENS_MAX), 2);
    tokens_expect(escaped, &t[1], 5, 10, true, "ab\"");
}

void Tokens_nested_brackets(void) {
    // command/tokens
    const char *line = "set e Position {x: [1, 2], y: (3 4)} tail";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 5);
    tokens_expect(line, &t[2], 6, 14, false, "Position");
    tokens_expect(line, &t[3], 15, 36, false, "{x: [1, 2], y: (3 4)}");
    tokens_expect(line, &t[4], 37, 41, false, "tail");

    const char *open = "set e P {x: 1";
    test_int(box_command_tokens(open, t, TOKENS_MAX), 4);
    tokens_expect(open, &t[3], 8, 13, false, "{x: 1");
}

void Tokens_quote_inside_word(void) {
    // command/tokens
    const char *line = "a\"b c\" \"d\"e";
    box_command_token_t t[TOKENS_MAX];
    test_int(box_command_tokens(line, t, TOKENS_MAX), 4);
    tokens_expect(line, &t[0], 0, 3, false, "a\"b");
    tokens_expect(line, &t[1], 4, 6, false, "c\"");
    tokens_expect(line, &t[2], 7, 10, true, "d");
    tokens_expect(line, &t[3], 10, 11, false, "e");
}

void Tokens_rest_end(void) {
    // command/tokens
    test_int(box_command_rest_end("query q (A, B) \t\n "), 14);
    test_int(box_command_rest_end("query"), 5);
    test_int(box_command_rest_end("   "), 0);
    test_int(box_command_rest_end(""), 0);
}

void Tokens_more_than_max(void) {
    // command/tokens
    const char *line = "a b c d e f g h i j k l m n o p q r s t";
    box_command_token_t t[4];
    test_int(box_command_tokens(line, t, 4), 20);
    tokens_expect(line, &t[3], 6, 7, false, "d");
}

void Tokens_text_matches_dispatcher(void) {
    // command/tokens
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_command_register(world, &(box_command_desc_t){
        .name = "tokens.echo",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t), .optional = true },
            { .name = "b", .type = ecs_id(ecs_string_t), .optional = true },
            { .name = "c", .type = ecs_id(ecs_string_t), .optional = true },
            { .name = "d", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = tokens_echo
    });

    const char *lines[] = {
        "tokens.echo \"two words\" {x: \"}\" 1} [a [b c]]",
        "tokens.echo \"esc \\\"q\\\" \\\\\" a\"b c\"",
        "tokens.echo \"\" (1 (2 3))  \"open ended",
        "  \"tokens.echo\"   x\ty\n"
    };

    ecs_entity_t s = box_terminal_new(world);
    int32_t i, j;
    for (i = 0; i < 4; i ++) {
        box_command_token_t t[TOKENS_MAX];
        int32_t count = box_command_tokens(lines[i], t, TOKENS_MAX);
        ecs_strbuf_t buf = ECS_STRBUF_INIT;
        for (j = 1; j < count; j ++) {
            char *text = box_command_token_text(lines[i], &t[j]);
            ecs_strbuf_appendstr(&buf, j > 1 ? "|" : "");
            ecs_strbuf_appendstr(&buf, text);
            ecs_os_free(text);
        }

        char *expect = ecs_strbuf_get(&buf);
        bool error = true;
        char *out = box_terminal_run(world, s, lines[i], &error);
        test_bool(error, false);
        test_str(out, expect ? expect : "");
        ecs_os_free(out);
        ecs_os_free(expect);
    }

    ecs_fini(world);
}

void Tokens_rest_matches_dispatcher(void) {
    // command/tokens
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_command_register(world, &(box_command_desc_t){
        .name = "tokens.rest",
        .params = {
            { .name = "a", .type = ecs_id(ecs_string_t) },
            { .name = "rest", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = tokens_echo
    });

    const char *line = "tokens.rest first \"b  c\"  {d e}  \"open \t ";
    box_command_token_t t[TOKENS_MAX];
    int32_t count = box_command_tokens(line, t, TOKENS_MAX);
    test_int(count, 5);
    int32_t end = box_command_rest_end(line);
    char *expect = flecs_asprintf("first|%.*s", end - t[2].start, &line[t[2].start]);
    ecs_entity_t s = box_terminal_new(world);
    char *out = box_terminal_run(world, s, line, NULL);
    test_str(out, expect);
    test_str(out, "first|\"b  c\"  {d e}  \"open");
    ecs_os_free(out);
    ecs_os_free(expect);

    const char *single = "tokens.rest first \"b  c\"";
    test_int(box_command_tokens(single, t, TOKENS_MAX), 3);
    out = box_terminal_run(world, s, single, NULL);
    test_str(out, "first|b  c");
    ecs_os_free(out);

    ecs_fini(world);
}

void Tokens_path_params(void) {
    // command/tokens
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t scope = ecs_lookup(world, BOX_COMMAND_SCOPE);
    const CommandInfo *get = ecs_get(world,
        ecs_lookup_child(world, scope, "get"), CommandInfo);
    test_int(get->paths, 1 << 1);
    const CommandInfo *mv = ecs_get(world,
        ecs_lookup_child(world, scope, "mv"), CommandInfo);
    test_int(mv->paths, 1 << 1);
    const CommandInfo *touch = ecs_get(world,
        ecs_lookup_child(world, scope, "touch"), CommandInfo);
    test_int(touch->paths, 0);
    ecs_entity_t scroll = ecs_lookup_path_w_sep(world, scope, "mouse.scroll", ".", NULL,
        false);
    test_int(ecs_get(world, scroll, CommandInfo)->paths, 0);

    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity_t e = 0;
    test_int(box_shell_lookup(world, 0, "things.alpha", &e), 0);
    test_assert(e == ecs_lookup(world, "things.alpha"));
    test_int(box_shell_lookup(world, ecs_lookup(world, "things"), "alpha", &e), 0);
    test_int(box_shell_lookup(world, ecs_lookup(world, "things"), "../things", &e), 0);
    test_int(box_shell_lookup(world, 0, "/", &e), 0);
    test_assert(e == 0);
    test_int(box_shell_lookup(world, 0, "things.beta", &e), -1);

    ecs_fini(world);
}
