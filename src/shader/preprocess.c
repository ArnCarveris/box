/**
 * @file shader/preprocess.c
 * @brief Text preprocessor of the shader module: includes, macros and conditionals.
 *
 * The preprocessor is line based. A line that starts with a hash is a
 * directive, every other line has its identifiers substituted and is appended
 * to the output. Errors carry the file and the line they happened on, which is
 * the file of the include the error sits in, not the file that included it.
 */

#include "shader_internal.h"
#include <stdio.h>
#include <stdlib.h>

/** Conditional blocks a file may nest */
#define BOX_PP_COND_MAX (16)

/** Longest error message the preprocessor reports */
#define BOX_PP_ERROR_MAX (512)

/** One macro of a preprocessor run */
typedef struct box_pp_macro_t {
    char *name;  /**< Identifier the substitution replaces */
    char *value; /**< Text it is replaced with */
    bool locked; /**< The component or the engine set it, so a file cannot replace it */
} box_pp_macro_t;

/** State of one preprocessor run */
typedef struct box_pp_t {
    ecs_world_t *world; /**< World the generated includes come from, may be NULL */
    ecs_strbuf_t out;   /**< Preprocessed source */
    ecs_vec_t macros;   /**< box_pp_macro_t defined so far */
    ecs_vec_t stack;    /**< char* of the files currently open, for cycle detection */
    ecs_vec_t once;     /**< char* of the files that asked to be included once */
    char *error;        /**< First error, NULL while the run is fine */
} box_pp_t;

/** State of one file of a preprocessor run */
typedef struct box_pp_file_t {
    const char *path;                /**< File the lines come from */
    int32_t line;                    /**< Line being processed, one based */
    int32_t depth;                   /**< Conditional blocks entered */
    bool active[BOX_PP_COND_MAX + 1]; /**< Whether the block at a depth emits */
    bool taken[BOX_PP_COND_MAX + 1];  /**< Whether a branch of the block already ran */
} box_pp_file_t;

/** Constants the engine shares with its shaders, predefined for every run */
typedef struct box_pp_builtin_t {
    const char *name;  /**< Macro name */
    const char *value; /**< Macro value */
} box_pp_builtin_t;

/** Turn the expansion of a macro into a string */
#define BOX_PP_STR_(x) #x

/** Spell the value of a C macro as the preprocessor stores it */
#define BOX_PP_STR(x) BOX_PP_STR_(x)

/** Constants the renderer and the shaders must agree on */
static const box_pp_builtin_t box_pp_builtins[] = {
    { "SHADOW_CASCADE_MAX", BOX_PP_STR(BOX_SHADOW_CASCADE_MAX) },
    { "LAYER_MAX", BOX_PP_STR(BOX_LAYER_MAX) },
    { "INSTANCE_WORDS", BOX_PP_STR(BOX_INSTANCE_WORDS) },
    { "INSTANCE_BOUNDS", BOX_PP_STR(BOX_INSTANCE_BOUNDS) },
    { "INSTANCE_MODE", BOX_PP_STR(BOX_INSTANCE_MODE) },
    { "MODE_NOCAST_SHIFT", BOX_PP_STR(BOX_MODE_NOCAST_SHIFT) },
    { "MODE_LAYER_SHIFT", BOX_PP_STR(BOX_MODE_LAYER_SHIFT) },
    { "FRUSTUM_PLANES", BOX_PP_STR(BOX_FRUSTUM_PLANES) },
    { "CULL_ARG_WORDS", BOX_PP_STR(BOX_CULL_ARG_WORDS) },
    { "CULL_GROUP", BOX_PP_STR(BOX_CULL_GROUP) },
    { "HIZ_GROUP", BOX_PP_STR(BOX_HIZ_GROUP) },
    { "BAKE_GROUP", BOX_PP_STR(BOX_BAKE_GROUP) },
    { "ENV_MIPS", BOX_PP_STR(BOX_ENV_MIPS) },
    { "ENV_SPECULAR_SAMPLES", BOX_PP_STR(BOX_ENV_SPECULAR_SAMPLES) },
    { "ENV_IRRADIANCE_SAMPLES", BOX_PP_STR(BOX_ENV_IRRADIANCE_SAMPLES) },
    { "ENV_BRDF_SAMPLES", BOX_PP_STR(BOX_ENV_BRDF_SAMPLES) },
    { "GLASS_REFRACT", BOX_PP_STR(BOX_GLASS_REFRACT) },
    { "EFFECT_PARAM_VECS", BOX_PP_STR(BOX_EFFECT_PARAM_VECS) }
};

static bool box_pp_text(
    box_pp_t *pp,
    const char *path,
    const char *text);

/** Strip the parentheses a C constant spells its value with.
 *
 * @param value The value as the stringified macro holds it
 * @param out Receives the bare value
 * @param size Bytes of the output buffer
 */
static void box_pp_bare(
    const char *value,
    char *out,
    size_t size)
{
    size_t len = strlen(value);
    if (len >= 2 && value[0] == '(' && value[len - 1] == ')') {
        value ++;
        len -= 2;
    }

    if (len >= size) {
        len = size - 1;
    }

    memcpy(out, value, len);
    out[len] = '\0';
}

/** Record the first error of a run.
 *
 * @param pp The run
 * @param path File the error is in
 * @param line Line the error is on
 * @param message What went wrong
 */
static void box_pp_error(
    box_pp_t *pp,
    const char *path,
    int32_t line,
    const char *message)
{
    if (pp->error) {
        goto done;
    }

    char buffer[BOX_PP_ERROR_MAX];
    snprintf(buffer, sizeof(buffer), "%s:%d: %s", path, line, message);
    pp->error = ecs_os_strdup(buffer);
done:
    return;
}

/** Check whether a character may appear in an identifier.
 *
 * @param c The character
 * @param first The character starts the identifier
 * @return true when it belongs to an identifier
 */
static bool box_pp_ident(
    char c,
    bool first)
{
    bool result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';

    return result || (!first && c >= '0' && c <= '9');
}

/** Find the value of a macro.
 *
 * @param pp The run
 * @param name Start of the name
 * @param len Characters of the name
 * @return The value, or NULL when the macro is not defined
 */
static const char* box_pp_lookup(
    box_pp_t *pp,
    const char *name,
    size_t len)
{
    const char *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&pp->macros) && !result; i ++) {
        box_pp_macro_t *m = ecs_vec_get_t(&pp->macros, box_pp_macro_t, i);
        if (!strncmp(m->name, name, len) && !m->name[len]) {
            result = m->value;
        }
    }

    return result;
}

/** Define a macro, replacing an earlier definition of the same name.
 *
 * @param pp The run
 * @param name Start of the name
 * @param len Characters of the name
 * @param value The value, an empty string when the define carries none
 * @param locked The macro comes from the component or the engine
 */
static void box_pp_define(
    box_pp_t *pp,
    const char *name,
    size_t len,
    const char *value,
    bool locked)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&pp->macros); i ++) {
        box_pp_macro_t *m = ecs_vec_get_t(&pp->macros, box_pp_macro_t, i);
        if (!strncmp(m->name, name, len) && !m->name[len]) {
            if (m->locked && !locked) {
                goto done;
            }

            ecs_os_free(m->value);
            m->value = ecs_os_strdup(value);
            m->locked = locked;
            goto done;
        }
    }

    box_pp_macro_t *created = ecs_vec_append_t(NULL, &pp->macros, box_pp_macro_t);
    created->name = ecs_os_memdup((void*)name, (ecs_size_t)len + 1);
    created->name[len] = '\0';
    created->value = ecs_os_strdup(value);
    created->locked = locked;
done:
    return;
}

/** Integer value a condition token has.
 *
 * @param pp The run
 * @param token Start of the token
 * @param len Characters of the token
 * @return The value, 0 for an undefined macro and for text that is not a number
 */
static long box_pp_value(
    box_pp_t *pp,
    const char *token,
    size_t len)
{
    long result = 0;
    if (len && token[0] >= '0' && token[0] <= '9') {
        result = strtol(token, NULL, 0);
        goto done;
    }

    const char *value = box_pp_lookup(pp, token, len);
    if (value) {
        result = strtol(value, NULL, 0);
    }

done:
    return result;
}

/** Append a line with its macros substituted.
 *
 * @param pp The run
 * @param line Start of the line
 * @param len Characters of the line, without its terminator
 */
static void box_pp_substitute(
    box_pp_t *pp,
    const char *line,
    size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (!box_pp_ident(line[i], true)) {
            ecs_strbuf_appendch(&pp->out, line[i]);
            i ++;
            continue;
        }

        size_t start = i;
        while (i < len && box_pp_ident(line[i], false)) {
            i ++;
        }

        const char *value = box_pp_lookup(pp, line + start, i - start);
        if (value) {
            ecs_strbuf_appendstr(&pp->out, value);
        } else {
            ecs_strbuf_appendstrn(&pp->out, line + start, (int32_t)(i - start));
        }
    }

    ecs_strbuf_appendch(&pp->out, '\n');
}

/** Skip spaces and tabs.
 *
 * @param text The text
 * @return The first character that is not a space
 */
static const char* box_pp_skip(
    const char *text)
{
    while (*text == ' ' || *text == '\t') {
        text ++;
    }

    return text;
}

/** Length of the identifier a text starts with.
 *
 * @param text The text
 * @return Characters of the identifier, 0 when it does not start with one
 */
static size_t box_pp_word(
    const char *text)
{
    size_t result = 0;
    while (box_pp_ident(text[result], result == 0)) {
        result ++;
    }

    return result;
}

/** Directory the includes of a file are relative to.
 *
 * @param path The file
 * @param out Receives the directory, empty when the file has none
 * @param size Bytes of the output buffer
 */
static void box_pp_dir(
    const char *path,
    char *out,
    size_t size)
{
    const char *slash = strrchr(path, '/');
    size_t len = slash ? (size_t)(slash - path) : 0;
    if (len >= size) {
        len = size - 1;
    }

    memcpy(out, path, len);
    out[len] = '\0';
}

/** Check whether a file is already open in this run.
 *
 * @param pp The run
 * @param path The resolved path
 * @return true when including it again would cycle
 */
static bool box_pp_open(
    box_pp_t *pp,
    const char *path)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&pp->stack) && !result; i ++) {
        result = !strcmp(*ecs_vec_get_t(&pp->stack, char*, i), path);
    }

    return result;
}

/** Check whether a file asked to be included once and already was.
 *
 * @param pp The run
 * @param path The resolved path
 * @return true when the file must be skipped
 */
static bool box_pp_seen(
    box_pp_t *pp,
    const char *path)
{
    bool result = false;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&pp->once) && !result; i ++) {
        result = !strcmp(*ecs_vec_get_t(&pp->once, char*, i), path);
    }

    return result;
}

/** Run an include directive.
 *
 * @param pp The run
 * @param f The file the directive is in
 * @param name The include name between its quotes
 */
static void box_pp_include(
    box_pp_t *pp,
    box_pp_file_t *f,
    const char *name)
{
    size_t prefix = strlen(BOX_SHADER_GENERATED);
    if (!strncmp(name, BOX_SHADER_GENERATED, prefix)) {
        char *source = box_shader_generated(pp->world, name + prefix);
        if (!source) {
            box_pp_error(pp, f->path, f->line, "unknown generated include");
            goto done;
        }

        box_pp_text(pp, name, source);
        ecs_os_free(source);
        goto done;
    }

    char dir[BOX_SHADER_PATH_MAX];
    char path[BOX_SHADER_PATH_MAX];
    box_pp_dir(f->path, dir, sizeof(dir));
    if (dir[0]) {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
    } else {
        snprintf(path, sizeof(path), "%s", name);
    }

    char resolved[BOX_SHADER_PATH_MAX];
    if (!box_shader_resolve(path, resolved, sizeof(resolved)) &&
        !box_shader_resolve(name, resolved, sizeof(resolved)))
    {
        box_pp_error(pp, f->path, f->line, "include not found");
        goto done;
    }

    if (box_pp_open(pp, resolved)) {
        box_pp_error(pp, f->path, f->line, "cyclic include");
        goto done;
    }

    if (box_pp_seen(pp, resolved)) {
        goto done;
    }

    char *text = box_shader_read(resolved);
    if (!text) {
        box_pp_error(pp, f->path, f->line, "include could not be read");
        goto done;
    }

    box_pp_text(pp, resolved, text);
    ecs_os_free(text);
done:
    return;
}

/** Push a conditional block onto the stack of a file.
 *
 * @param pp The run
 * @param f The file
 * @param value The condition held
 */
static void box_pp_push(
    box_pp_t *pp,
    box_pp_file_t *f,
    bool value)
{
    if (f->depth >= BOX_PP_COND_MAX) {
        box_pp_error(pp, f->path, f->line, "conditional blocks nested too deeply");
        goto done;
    }

    bool parent = f->active[f->depth];
    f->depth ++;
    f->taken[f->depth] = parent && value;
    f->active[f->depth] = parent && value;
done:
    return;
}

/** Run a conditional directive.
 *
 * @param pp The run
 * @param f The file the directive is in
 * @param word The directive name
 * @param len Characters of the directive name
 * @param rest The text after the directive
 * @return true when the directive was a conditional
 */
static bool box_pp_conditional(
    box_pp_t *pp,
    box_pp_file_t *f,
    const char *word,
    size_t len,
    const char *rest)
{
    bool result = true;
    size_t token = box_pp_word(rest);
    if (len == 2 && !strncmp(word, "if", len)) {
        box_pp_push(pp, f, box_pp_value(pp, rest, token) != 0);
    } else if (len == 5 && !strncmp(word, "ifdef", len)) {
        box_pp_push(pp, f, box_pp_lookup(pp, rest, token) != NULL);
    } else if (len == 6 && !strncmp(word, "ifndef", len)) {
        box_pp_push(pp, f, box_pp_lookup(pp, rest, token) == NULL);
    } else if (len == 4 && !strncmp(word, "else", len)) {
        if (!f->depth) {
            box_pp_error(pp, f->path, f->line, "else without if");
        } else {
            bool parent = f->active[f->depth - 1];
            f->active[f->depth] = parent && !f->taken[f->depth];
            f->taken[f->depth] = true;
        }
    } else if (len == 5 && !strncmp(word, "endif", len)) {
        if (!f->depth) {
            box_pp_error(pp, f->path, f->line, "endif without if");
        } else {
            f->depth --;
        }
    } else {
        result = false;
    }

    return result;
}

/** Run a directive line.
 *
 * @param pp The run
 * @param f The file the line is in
 * @param line The line without its terminator
 */
static void box_pp_directive(
    box_pp_t *pp,
    box_pp_file_t *f,
    const char *line)
{
    const char *word = box_pp_skip(line + 1);
    size_t len = box_pp_word(word);
    const char *rest = box_pp_skip(word + len);
    if (box_pp_conditional(pp, f, word, len, rest)) {
        goto done;
    }

    if (!f->active[f->depth]) {
        goto done;
    }

    if (len == 7 && !strncmp(word, "include", len)) {
        char name[BOX_SHADER_PATH_MAX];
        const char *end = *rest == '"' ? strchr(rest + 1, '"') : NULL;
        if (!end) {
            box_pp_error(pp, f->path, f->line, "include expects a quoted file");
            goto done;
        }

        size_t count = (size_t)(end - rest - 1);
        memcpy(name, rest + 1, count);
        name[count] = '\0';
        box_pp_include(pp, f, name);
    } else if (len == 6 && !strncmp(word, "define", len)) {
        size_t token = box_pp_word(rest);
        if (!token) {
            box_pp_error(pp, f->path, f->line, "define expects a name");
            goto done;
        }

        box_pp_define(pp, rest, token, box_pp_skip(rest + token), false);
    } else if (len == 6 && !strncmp(word, "pragma", len)) {
        if (strncmp(rest, "once", 4)) {
            box_pp_error(pp, f->path, f->line, "unknown pragma");
            goto done;
        }

        *(char**)ecs_vec_append_t(NULL, &pp->once, char*) = ecs_os_strdup(f->path);
    } else {
        box_pp_error(pp, f->path, f->line, "unknown directive");
    }

done:
    return;
}

/** Run the lines of one file or generated include.
 *
 * @param pp The run
 * @param path The file, which the errors of its lines name
 * @param text The contents
 * @return true when the text was processed without an error
 */
static bool box_pp_text(
    box_pp_t *pp,
    const char *path,
    const char *text)
{
    box_pp_file_t f = { .path = path, .line = 0, .depth = 0 };
    f.active[0] = true;
    *(char**)ecs_vec_append_t(NULL, &pp->stack, char*) = ecs_os_strdup(path);

    const char *cursor = text;
    while (*cursor && !pp->error) {
        const char *end = strchr(cursor, '\n');
        size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
        f.line ++;
        if (*box_pp_skip(cursor) == '#') {
            char line[BOX_SHADER_PATH_MAX];
            size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cursor, copy);
            line[copy] = '\0';
            box_pp_directive(pp, &f, box_pp_skip(line));
        } else if (f.active[f.depth]) {
            box_pp_substitute(pp, cursor, len);
        }

        cursor = end ? end + 1 : cursor + len;
    }

    if (f.depth && !pp->error) {
        box_pp_error(pp, path, f.line, "if without endif");
    }

    int32_t last = ecs_vec_count(&pp->stack) - 1;
    ecs_os_free(*ecs_vec_get_t(&pp->stack, char*, last));
    ecs_vec_remove_last(&pp->stack);

    return pp->error == NULL;
}

/** Define the macros a comma separated NAME=value list holds.
 *
 * @param pp The run
 * @param macros The list, NULL or empty for none
 */
static void box_pp_macros(
    box_pp_t *pp,
    const char *macros)
{
    const char *cursor = macros;
    while (cursor && *cursor) {
        const char *end = strchr(cursor, ',');
        const char *stop = end ? end : cursor + strlen(cursor);
        const char *name = box_pp_skip(cursor);
        size_t len = box_pp_word(name);
        if (len) {
            const char *eq = memchr(name, '=', (size_t)(stop - name));
            char value[BOX_PP_ERROR_MAX];
            value[0] = '1';
            value[1] = '\0';
            if (eq) {
                size_t count = (size_t)(stop - eq - 1);
                if (count >= sizeof(value)) {
                    count = sizeof(value) - 1;
                }

                memcpy(value, eq + 1, count);
                value[count] = '\0';
            }

            box_pp_define(pp, name, len, value, true);
        }

        cursor = end ? end + 1 : stop;
    }
}

/** Release the state of a run.
 *
 * @param pp The run
 */
static void box_pp_fini(
    box_pp_t *pp)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&pp->macros); i ++) {
        box_pp_macro_t *m = ecs_vec_get_t(&pp->macros, box_pp_macro_t, i);
        ecs_os_free(m->name);
        ecs_os_free(m->value);
    }

    for (i = 0; i < ecs_vec_count(&pp->stack); i ++) {
        ecs_os_free(*ecs_vec_get_t(&pp->stack, char*, i));
    }

    for (i = 0; i < ecs_vec_count(&pp->once); i ++) {
        ecs_os_free(*ecs_vec_get_t(&pp->once, char*, i));
    }

    ecs_vec_fini_t(NULL, &pp->macros, box_pp_macro_t);
    ecs_vec_fini_t(NULL, &pp->stack, char*);
    ecs_vec_fini_t(NULL, &pp->once, char*);
}

void box_shader_preprocess(
    ecs_world_t *world,
    const char *path,
    const char *macros,
    box_shader_result_t *out)
{
    box_pp_t pp = { .world = world, .out = ECS_STRBUF_INIT };
    ecs_vec_init_t(NULL, &pp.macros, box_pp_macro_t, 0);
    ecs_vec_init_t(NULL, &pp.stack, char*, 0);
    ecs_vec_init_t(NULL, &pp.once, char*, 0);
    out->source = NULL;
    out->error = NULL;

    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(box_pp_builtins) / sizeof(box_pp_builtin_t)); i ++) {
        const box_pp_builtin_t *b = &box_pp_builtins[i];
        char value[BOX_PP_ERROR_MAX];
        box_pp_bare(b->value, value, sizeof(value));
        box_pp_define(&pp, b->name, strlen(b->name), value, true);
    }

    box_pp_macros(&pp, macros);
    char *text = box_shader_read(path);
    if (!text) {
        box_pp_error(&pp, path, 0, "file could not be read");
    } else {
        box_pp_text(&pp, path, text);
        ecs_os_free(text);
    }

    if (pp.error) {
        out->error = pp.error;
        ecs_strbuf_reset(&pp.out);
    } else {
        out->source = ecs_strbuf_get(&pp.out);
    }

    box_pp_fini(&pp);
}

void box_shader_result_fini(
    box_shader_result_t *result)
{
    ecs_os_free(result->source);
    ecs_os_free(result->error);
    result->source = NULL;
    result->error = NULL;
}
