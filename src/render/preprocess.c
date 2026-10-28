/**
 * @file render/preprocess.c
 * @brief WGSL preprocessor: #include, #define, #undef, #ifdef, #ifndef, #else, #endif, #pragma.
 *
 * Also finds files on the search path of the engine and stamps them so changes can be seen.
 */

#include "preprocess.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/** Longest path of a folder on the search path */
#define BOX_SHADER_PATH_MAX (4096)

/** A macro: a name and the text it expands to */
typedef struct box_shader_macro_t {
    char *name;     /**< Name of the macro */
    char *value;    /**< Text the name expands to, empty for none */
} box_shader_macro_t;

/** An open #ifdef or #ifndef */
typedef struct box_shader_cond_t {
    int32_t line;   /**< Line of the directive */
    bool ifndef;    /**< The directive is #ifndef */
    bool parent;    /**< The region around the directive is active */
    bool cond;      /**< The condition of the directive holds */
    bool in_else;   /**< The #else of the directive was seen */
} box_shader_cond_t;

/** A directive line */
typedef struct box_shader_directive_t {
    const char *file;   /**< File of the line */
    int32_t line;       /**< Line number */
    const char *name;   /**< Name of the directive after the # */
    int32_t name_len;   /**< Length of the name */
    const char *arg;    /**< Text after the name, without surrounding space and comment */
    int32_t arg_len;    /**< Length of the text */
    int32_t base;       /**< Open conditionals when the file started */
} box_shader_directive_t;

/** State of one preprocessing */
typedef struct box_shader_pp_t {
    const box_shader_desc_t *desc;  /**< What is preprocessed */
    box_shader_output_t *out;       /**< The result */
    ecs_strbuf_t code;              /**< Code written so far */
    ecs_vec_t macros;               /**< box_shader_macro_t */
    ecs_vec_t conds;                /**< box_shader_cond_t */
    ecs_vec_t stack;                /**< const char*, the files being processed */
    ecs_vec_t once;                 /**< char*, the files with #pragma once */
    int32_t expanding[BOX_SHADER_EXPAND_DEPTH]; /**< Macros being expanded */
    int32_t expand_depth;           /**< Entries in expanding */
    bool failed;                    /**< An error was reported */
} box_shader_pp_t;

/** Copy part of a string.
 *
 * @param str The string
 * @param len Characters to copy
 * @return The terminated copy
 */
static char* box_shader_strndup(
    const char *str,
    int32_t len)
{
    char *result = ecs_os_malloc(len + 1);
    ecs_os_memcpy(result, str, len);
    result[len] = '\0';

    return result;
}

/** Report the first error of a preprocessing.
 *
 * @param pp The preprocessing
 * @param file File of the error
 * @param line Line of the error, 0 for none
 * @param fmt Format of the message
 * @param ... Arguments of the format
 */
static void box_shader_error(
    box_shader_pp_t *pp,
    const char *file,
    int32_t line,
    const char *fmt,
    ...)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    va_list args;
    if (pp->failed) {
        goto done;
    }

    if (line) {
        ecs_strbuf_append(&buf, "%s:%d: ", file, line);
    } else {
        ecs_strbuf_append(&buf, "%s: ", file);
    }

    va_start(args, fmt);
    ecs_strbuf_vappend(&buf, fmt, args);
    va_end(args);
    pp->out->error = ecs_strbuf_get(&buf);
    pp->failed = true;
done:
    return;
}

/** Get the length of the identifier a string starts with.
 *
 * @param str The string
 * @param len Length of the string
 * @return Characters of the identifier, 0 when the string does not start with one
 */
static int32_t box_shader_ident_len(
    const char *str,
    int32_t len)
{
    int32_t result = 0;
    if (!len || !(isalpha((unsigned char)str[0]) || str[0] == '_')) {
        goto done;
    }

    while (result < len && (isalnum((unsigned char)str[result]) || str[result] == '_')) {
        result ++;
    }

done:
    return result;
}

/** Find a macro by name.
 *
 * @param pp The preprocessing
 * @param name The name, not terminated
 * @param len Length of the name
 * @return Index of the macro, -1 when it is not defined
 */
static int32_t box_shader_macro_find(
    box_shader_pp_t *pp,
    const char *name,
    int32_t len)
{
    int32_t i, count = ecs_vec_count(&pp->macros);
    int32_t result = -1;
    box_shader_macro_t *macros = ecs_vec_first(&pp->macros);
    for (i = 0; i < count; i ++) {
        if (!strncmp(macros[i].name, name, len) && !macros[i].name[len]) {
            result = i;
            break;
        }
    }

    return result;
}

/** Define a macro or replace its value.
 *
 * @param pp The preprocessing
 * @param name The name, not terminated
 * @param name_len Length of the name
 * @param value The value, not terminated
 * @param value_len Length of the value
 */
static void box_shader_macro_set(
    box_shader_pp_t *pp,
    const char *name,
    int32_t name_len,
    const char *value,
    int32_t value_len)
{
    int32_t index = box_shader_macro_find(pp, name, name_len);
    box_shader_macro_t *m;
    if (index == -1) {
        m = ecs_vec_append_t(NULL, &pp->macros, box_shader_macro_t);
        m->name = box_shader_strndup(name, name_len);
    } else {
        m = ecs_vec_get_t(&pp->macros, box_shader_macro_t, index);
        ecs_os_free(m->value);
    }

    m->value = box_shader_strndup(value, value_len);
}

/** Remove a macro when it is defined.
 *
 * @param pp The preprocessing
 * @param name The name, not terminated
 * @param len Length of the name
 */
static void box_shader_macro_unset(
    box_shader_pp_t *pp,
    const char *name,
    int32_t len)
{
    int32_t index = box_shader_macro_find(pp, name, len);
    if (index != -1) {
        box_shader_macro_t *m = ecs_vec_get_t(&pp->macros, box_shader_macro_t, index);
        ecs_os_free(m->name);
        ecs_os_free(m->value);
        ecs_vec_remove_t(&pp->macros, box_shader_macro_t, index);
    }
}

/** Write text to the code with every macro expanded, except one inside its own expansion.
 *
 * @param pp The preprocessing
 * @param text The text
 * @param len Length of the text
 */
static void box_shader_expand(
    box_shader_pp_t *pp,
    const char *text,
    int32_t len)
{
    int32_t i = 0, j;
    while (i < len) {
        int32_t ident = box_shader_ident_len(text + i, len - i);
        if (!ident && isdigit((unsigned char)text[i])) {
            ident = 1;
            while (i + ident < len && (isalnum((unsigned char)text[i + ident]) ||
                text[i + ident] == '_' || text[i + ident] == '.'))
            {
                ident ++;
            }

            ecs_strbuf_appendstrn(&pp->code, text + i, ident);
            i += ident;
            continue;
        }

        if (!ident) {
            ecs_strbuf_appendch(&pp->code, text[i]);
            i ++;
            continue;
        }

        int32_t m = box_shader_macro_find(pp, text + i, ident);
        bool skip = m == -1 || pp->expand_depth == BOX_SHADER_EXPAND_DEPTH;
        for (j = 0; !skip && j < pp->expand_depth; j ++) {
            skip = pp->expanding[j] == m;
        }

        if (skip) {
            ecs_strbuf_appendstrn(&pp->code, text + i, ident);
        } else {
            const char *value = ecs_vec_get_t(&pp->macros, box_shader_macro_t, m)->value;
            pp->expanding[pp->expand_depth ++] = m;
            box_shader_expand(pp, value, ecs_os_strlen(value));
            pp->expand_depth --;
        }

        i += ident;
    }
}

/** Check whether the current line is outside every false conditional.
 *
 * @param pp The preprocessing
 * @return Whether lines are written
 */
static bool box_shader_active(
    box_shader_pp_t *pp)
{
    bool result = true;
    if (ecs_vec_count(&pp->conds)) {
        box_shader_cond_t *c = ecs_vec_last_t(&pp->conds, box_shader_cond_t);
        result = c->parent && (c->in_else ? !c->cond : c->cond);
    }

    return result;
}

/** Check whether a path is a regular file.
 *
 * @param path The path
 * @return Whether the file exists
 */
static bool box_shader_readable(
    const char *path)
{
    struct stat st;

    return !stat(path, &st) && S_ISREG(st.st_mode);
}

/** Cut the last component off a folder.
 *
 * @param dir The folder, shortened in place
 * @return Whether a component was removed
 */
static bool box_shader_parent(
    char *dir)
{
    char *slash = strrchr(dir, '/');
    bool result = slash != NULL && slash != dir;
    if (result) {
        *slash = '\0';
    }

    return result;
}

/** Look for a relative path in a folder and its ancestors.
 *
 * @param dir The folder, shortened in place
 * @param path The relative path
 * @return The path the file was found at, NULL when it was not found
 */
static char* box_shader_search(
    char *dir,
    const char *path)
{
    char *result = NULL;
    do {
        result = flecs_asprintf("%s/%s", dir, path);
        if (box_shader_readable(result)) {
            break;
        }

        ecs_os_free(result);
        result = NULL;
    } while (box_shader_parent(dir));

    return result;
}

/** Get the folder of the running executable.
 *
 * @param dir Receives the folder
 * @param size Bytes of dir
 * @return Whether the folder is known
 */
static bool box_shader_exe_dir(
    char *dir,
    size_t size)
{
    bool result = false;
#if defined(__APPLE__)
    uint32_t len = (uint32_t)size;
    result = !_NSGetExecutablePath(dir, &len);
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    ssize_t len = readlink("/proc/self/exe", dir, size - 1);
    result = len > 0;
    if (result) {
        dir[len] = '\0';
    }

#else
    (void)dir;
    (void)size;
#endif
    if (result) {
        result = box_shader_parent(dir);
    }

    return result;
}

char* box_shader_locate_cwd(
    const char *path)
{
    char *result = NULL;
    char dir[BOX_SHADER_PATH_MAX];
    if (!path || !path[0]) {
        goto done;
    }

    if (path[0] == '/' || box_shader_readable(path)) {
        result = box_shader_readable(path) ? ecs_os_strdup(path) : NULL;
        goto done;
    }

    if (getcwd(dir, sizeof(dir))) {
        result = box_shader_search(dir, path);
    }

done:
    return result;
}

char* box_shader_locate(
    const char *path)
{
    char *result = box_shader_locate_cwd(path);
    char dir[BOX_SHADER_PATH_MAX];
    if (!result && path && path[0] && path[0] != '/' && box_shader_exe_dir(dir, sizeof(dir))) {
        result = box_shader_search(dir, path);
    }

    return result;
}

char* box_shader_read_file(
    const char *path)
{
    char *result = NULL;
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size >= 0) {
        result = ecs_os_malloc((ecs_size_t)size + 1);
        result[fread(result, 1, (size_t)size, f)] = '\0';
    }

    fclose(f);
done:
    return result;
}

void box_shader_stamp(
    const char *path,
    box_shader_stamp_t *stamp)
{
    struct stat st;
    ecs_os_memset_t(stamp, 0, box_shader_stamp_t);
    stamp->sec = -1;
    if (stat(path, &st)) {
        goto done;
    }

#ifdef __APPLE__
    stamp->sec = (int64_t)st.st_mtimespec.tv_sec;
    stamp->nsec = (int64_t)st.st_mtimespec.tv_nsec;
#else
    stamp->sec = (int64_t)st.st_mtim.tv_sec;
    stamp->nsec = (int64_t)st.st_mtim.tv_nsec;
#endif
    stamp->size = (int64_t)st.st_size;
done:
    return;
}

bool box_shader_stamp_equals(
    const box_shader_stamp_t *a,
    const box_shader_stamp_t *b)
{
    return a->sec == b->sec && a->nsec == b->nsec && a->size == b->size;
}

/** Add a file to the files of the result once.
 *
 * @param pp The preprocessing
 * @param path The file
 * @param stamp Its stamp
 */
static void box_shader_add_file(
    box_shader_pp_t *pp,
    const char *path,
    const box_shader_stamp_t *stamp)
{
    int32_t i, count = ecs_vec_count(&pp->out->files);
    box_shader_file_t *files = ecs_vec_first(&pp->out->files);
    for (i = 0; i < count; i ++) {
        if (!strcmp(files[i].path, path)) {
            goto done;
        }
    }

    box_shader_file_t *f = ecs_vec_append_t(NULL, &pp->out->files, box_shader_file_t);
    f->path = ecs_os_strdup(path);
    f->stamp = *stamp;
done:
    return;
}

/** Read one candidate of a file and remember it.
 *
 * @param pp The preprocessing
 * @param path The candidate
 * @return The contents, NULL when the candidate does not exist
 */
static char* box_shader_try(
    box_shader_pp_t *pp,
    const char *path)
{
    box_shader_stamp_t stamp = {0};
    char *result;
    if (pp->desc->read) {
        result = pp->desc->read(pp->desc->ctx, path);
    } else {
        box_shader_stamp(path, &stamp);
        result = box_shader_read_file(path);
    }

    if (result) {
        box_shader_add_file(pp, path, &stamp);
    }

    return result;
}

/** Read a file, first next to the file that includes it, then on the search path.
 *
 * @param pp The preprocessing
 * @param from The including file, NULL for the main file
 * @param path The file as written
 * @param resolved Receives the path the file was read from
 * @return The contents, NULL when the file was not found
 */
static char* box_shader_load(
    box_shader_pp_t *pp,
    const char *from,
    const char *path,
    char **resolved)
{
    char *result = NULL;
    char *first = NULL;
    char *candidate = NULL;
    const char *slash = from && path[0] != '/' ? strrchr(from, '/') : NULL;
    if (slash) {
        first = flecs_asprintf("%.*s%s", (int)(slash - from + 1), from, path);
        result = box_shader_try(pp, first);
        if (result) {
            *resolved = first;
            goto done;
        }
    }

    candidate = pp->desc->read ? ecs_os_strdup(path) : box_shader_locate(path);
    result = candidate ? box_shader_try(pp, candidate) : NULL;
    if (result) {
        *resolved = candidate;
        candidate = NULL;
    } else if (!pp->desc->read) {
        box_shader_stamp_t missing = { .sec = -1 };
        box_shader_add_file(pp, first ? first : path, &missing);
    }

    ecs_os_free(first);
    ecs_os_free(candidate);
done:
    return result;
}

/** Check whether a list of paths holds a path.
 *
 * @param list The paths, char* or const char*
 * @param path The path
 * @return Whether the path is in the list
 */
static bool box_shader_listed(
    ecs_vec_t *list,
    const char *path)
{
    int32_t i, count = ecs_vec_count(list);
    const char **paths = ecs_vec_first(list);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = !strcmp(paths[i], path);
    }

    return result;
}

/** Check whether a directive has a name.
 *
 * @param d The directive
 * @param name The name
 * @return Whether the directive has the name
 */
static bool box_shader_is(
    const box_shader_directive_t *d,
    const char *name)
{
    return d->name_len == ecs_os_strlen(name) && !strncmp(d->name, name, d->name_len);
}

/** Handle #ifdef, #ifndef, #else and #endif.
 *
 * @param pp The preprocessing
 * @param d The directive
 * @return Whether the directive was a conditional
 */
static bool box_shader_conditional(
    box_shader_pp_t *pp,
    const box_shader_directive_t *d)
{
    bool ifdef = box_shader_is(d, "ifdef"), ifndef = box_shader_is(d, "ifndef");
    bool result = true;
    box_shader_cond_t *top = ecs_vec_count(&pp->conds) > d->base
        ? ecs_vec_last_t(&pp->conds, box_shader_cond_t)
        : NULL;
    if (ifdef || ifndef) {
        int32_t len = box_shader_ident_len(d->arg, d->arg_len);
        if (!len) {
            box_shader_error(pp, d->file, d->line, "#%s expects a macro name",
                ifdef ? "ifdef" : "ifndef");
            goto done;
        }

        bool active = box_shader_active(pp);
        bool defined = box_shader_macro_find(pp, d->arg, len) != -1;
        box_shader_cond_t *c = ecs_vec_append_t(NULL, &pp->conds, box_shader_cond_t);
        c->line = d->line;
        c->ifndef = ifndef;
        c->parent = active;
        c->cond = ifdef ? defined : !defined;
        c->in_else = false;
    } else if (box_shader_is(d, "else")) {
        if (!top) {
            box_shader_error(pp, d->file, d->line, "#else without #ifdef");
        } else if (top->in_else) {
            box_shader_error(pp, d->file, d->line, "#else after #else");
        } else {
            top->in_else = true;
        }
    } else if (box_shader_is(d, "endif")) {
        if (!top) {
            box_shader_error(pp, d->file, d->line, "#endif without #ifdef");
        } else {
            ecs_vec_remove_last(&pp->conds);
        }
    } else {
        result = false;
    }

done:
    return result;
}

/** Handle #define and #undef.
 *
 * @param pp The preprocessing
 * @param d The directive
 */
static void box_shader_define(
    box_shader_pp_t *pp,
    const box_shader_directive_t *d)
{
    int32_t len = box_shader_ident_len(d->arg, d->arg_len);
    bool define = box_shader_is(d, "define");
    if (!len) {
        box_shader_error(pp, d->file, d->line, "#%s expects a macro name",
            define ? "define" : "undef");
        goto done;
    }

    if (!define) {
        box_shader_macro_unset(pp, d->arg, len);
        goto done;
    }

    if (len < d->arg_len && d->arg[len] == '(') {
        box_shader_error(pp, d->file, d->line, "function-like macros are not supported");
        goto done;
    }

    const char *value = d->arg + len;
    int32_t value_len = d->arg_len - len;
    while (value_len && isspace((unsigned char)value[0])) {
        value ++;
        value_len --;
    }

    box_shader_macro_set(pp, d->arg, len, value, value_len);
done:
    return;
}

/** Process a file and everything it includes.
 *
 * @param pp The preprocessing
 * @param from The including file, NULL for the main file
 * @param line Line of the #include in the including file
 * @param path The file as written
 */
static void box_shader_process(
    box_shader_pp_t *pp,
    const char *from,
    int32_t line,
    const char *path);

/** Handle #include.
 *
 * @param pp The preprocessing
 * @param d The directive
 */
static void box_shader_include(
    box_shader_pp_t *pp,
    const box_shader_directive_t *d)
{
    char close = d->arg_len ? (d->arg[0] == '<' ? '>' : d->arg[0]) : 0;
    if (d->arg_len < 3 || (d->arg[0] != '"' && d->arg[0] != '<') ||
        d->arg[d->arg_len - 1] != close)
    {
        box_shader_error(pp, d->file, d->line, "#include expects \"file\"");
        goto done;
    }

    char *path = box_shader_strndup(d->arg + 1, d->arg_len - 2);
    box_shader_process(pp, d->file, d->line, path);
    ecs_os_free(path);
done:
    return;
}

/** Handle a directive line.
 *
 * @param pp The preprocessing
 * @param d The directive
 */
static void box_shader_directive(
    box_shader_pp_t *pp,
    const box_shader_directive_t *d)
{
    if (box_shader_conditional(pp, d) || !box_shader_active(pp)) {
        goto done;
    }

    if (box_shader_is(d, "include")) {
        box_shader_include(pp, d);
    } else if (box_shader_is(d, "define") || box_shader_is(d, "undef")) {
        box_shader_define(pp, d);
    } else if (box_shader_is(d, "pragma") && d->arg_len == 4 && !strncmp(d->arg, "once", 4)) {
        if (!box_shader_listed(&pp->once, d->file)) {
            *ecs_vec_append_t(NULL, &pp->once, char*) = ecs_os_strdup(d->file);
        }
    } else if (box_shader_is(d, "pragma")) {
        box_shader_error(pp, d->file, d->line, "unknown pragma '%.*s'", d->arg_len, d->arg);
    } else {
        box_shader_error(pp, d->file, d->line, "unknown directive '#%.*s'",
            d->name_len, d->name);
    }

done:
    return;
}

/** Record the source position of the line that is about to be written.
 *
 * @param pp The preprocessing
 * @param file The file of the line
 * @param line The line number in the file
 */
static void box_shader_locate_line(
    box_shader_pp_t *pp,
    const char *file,
    int32_t line)
{
    int32_t i, count = ecs_vec_count(&pp->out->sources);
    char **sources = ecs_vec_first(&pp->out->sources);
    box_shader_loc_t *loc;
    for (i = 0; i < count; i ++) {
        if (!strcmp(sources[i], file)) {
            break;
        }
    }

    if (i == count) {
        *ecs_vec_append_t(NULL, &pp->out->sources, char*) = ecs_os_strdup(file);
    }

    loc = ecs_vec_append_t(NULL, &pp->out->lines, box_shader_loc_t);
    loc->file = i;
    loc->line = line;
}

/** Process one line of a file.
 *
 * @param pp The preprocessing
 * @param d The file, line number and conditional base of the line
 * @param text The line
 * @param len Length of the line without its newline
 */
static void box_shader_line(
    box_shader_pp_t *pp,
    box_shader_directive_t *d,
    const char *text,
    int32_t len)
{
    const char *p = text, *end = text + len;
    while (p < end && (*p == ' ' || *p == '\t')) {
        p ++;
    }

    if (p == end || *p != '#') {
        if (box_shader_active(pp)) {
            box_shader_expand(pp, text, len);
            ecs_strbuf_appendch(&pp->code, '\n');
            box_shader_locate_line(pp, d->file, d->line);
        }

        goto done;
    }

    p ++;
    while (p < end && (*p == ' ' || *p == '\t')) {
        p ++;
    }

    d->name = p;
    d->name_len = box_shader_ident_len(p, (int32_t)(end - p));
    p += d->name_len;
    while (p < end && isspace((unsigned char)*p)) {
        p ++;
    }

    const char *arg_end = p;
    while (arg_end < end && !(arg_end[0] == '/' && arg_end + 1 < end && arg_end[1] == '/')) {
        arg_end ++;
    }

    while (arg_end > p && isspace((unsigned char)arg_end[-1])) {
        arg_end --;
    }

    d->arg = p;
    d->arg_len = (int32_t)(arg_end - p);
    box_shader_directive(pp, d);
done:
    return;
}

/** Process the lines of a file.
 *
 * @param pp The preprocessing
 * @param path The file
 * @param text Its contents
 */
static void box_shader_lines(
    box_shader_pp_t *pp,
    const char *path,
    const char *text)
{
    box_shader_directive_t d = { .file = path, .base = ecs_vec_count(&pp->conds) };
    const char *ptr = text;
    while (*ptr && !pp->failed) {
        const char *end = strchr(ptr, '\n');
        int32_t len;
        if (!end) {
            end = ptr + ecs_os_strlen(ptr);
        }

        len = (int32_t)(end - ptr);
        if (len && ptr[len - 1] == '\r') {
            len --;
        }

        d.line ++;
        box_shader_line(pp, &d, ptr, len);
        ptr = *end ? end + 1 : end;
    }

    if (ecs_vec_count(&pp->conds) > d.base) {
        box_shader_cond_t *c = ecs_vec_last_t(&pp->conds, box_shader_cond_t);
        box_shader_error(pp, path, c->line, "#%s without #endif",
            c->ifndef ? "ifndef" : "ifdef");
    }

    ecs_vec_set_count_t(NULL, &pp->conds, box_shader_cond_t, d.base);
}

/** Process a file and everything it includes.
 *
 * @param pp The preprocessing
 * @param from The including file, NULL for the main file
 * @param line Line of the #include in the including file
 * @param path The file as written
 */
static void box_shader_process(
    box_shader_pp_t *pp,
    const char *from,
    int32_t line,
    const char *path)
{
    char *resolved = NULL;
    char *text = box_shader_load(pp, from, path, &resolved);
    if (!text) {
        if (from) {
            box_shader_error(pp, from, line, "cannot find include '%s'", path);
        } else {
            box_shader_error(pp, path, 0, "cannot read file");
        }

        goto done;
    }

    if (box_shader_listed(&pp->once, resolved)) {
        goto done;
    }

    if (box_shader_listed(&pp->stack, resolved)) {
        box_shader_error(pp, from, line, "recursive include of '%s'", path);
        goto done;
    }

    if (ecs_vec_count(&pp->stack) == BOX_SHADER_INCLUDE_DEPTH) {
        box_shader_error(pp, from, line, "includes nest deeper than %d",
            BOX_SHADER_INCLUDE_DEPTH);
        goto done;
    }

    *ecs_vec_append_t(NULL, &pp->stack, const char*) = resolved;
    box_shader_lines(pp, resolved, text);
    ecs_vec_remove_last(&pp->stack);
done:
    ecs_os_free(text);
    ecs_os_free(resolved);
}

/** Define the macros of the defines of the description.
 *
 * @param pp The preprocessing
 */
static void box_shader_defines(
    box_shader_pp_t *pp)
{
    const char *p = pp->desc->defines;
    while (p && *p && !pp->failed) {
        while (*p == ',' || *p == ';' || isspace((unsigned char)*p)) {
            p ++;
        }

        const char *start = p;
        while (*p && *p != ',' && *p != ';' && !isspace((unsigned char)*p)) {
            p ++;
        }

        int32_t len = (int32_t)(p - start);
        int32_t name = box_shader_ident_len(start, len);
        if (!len) {
            continue;
        }

        if (!name || (name < len && start[name] != '=')) {
            box_shader_error(pp, pp->desc->path, 0, "invalid define '%.*s'", len, start);
            continue;
        }

        int32_t value = name < len ? name + 1 : len;
        box_shader_macro_set(pp, start, name, start + value, len - value);
    }
}

int box_shader_preprocess(
    const box_shader_desc_t *desc,
    box_shader_output_t *out)
{
    box_shader_pp_t pp = { .desc = desc, .out = out, .code = ECS_STRBUF_INIT };
    int32_t i;
    ecs_os_memset_t(out, 0, box_shader_output_t);
    ecs_vec_init_t(NULL, &out->files, box_shader_file_t, 0);
    ecs_vec_init_t(NULL, &out->sources, char*, 0);
    ecs_vec_init_t(NULL, &out->lines, box_shader_loc_t, 0);
    ecs_vec_init_t(NULL, &pp.macros, box_shader_macro_t, 0);
    ecs_vec_init_t(NULL, &pp.conds, box_shader_cond_t, 0);
    ecs_vec_init_t(NULL, &pp.stack, const char*, 0);
    ecs_vec_init_t(NULL, &pp.once, char*, 0);
    if (!desc->path || !desc->path[0]) {
        box_shader_error(&pp, "shader", 0, "no path");
    }

    box_shader_defines(&pp);
    if (!pp.failed) {
        box_shader_process(&pp, NULL, 0, desc->path);
    }

    if (pp.failed) {
        ecs_strbuf_reset(&pp.code);
    } else {
        out->code = ecs_strbuf_get(&pp.code);
        out->code = out->code ? out->code : ecs_os_strdup("");
    }

    for (i = 0; i < ecs_vec_count(&pp.macros); i ++) {
        box_shader_macro_t *m = ecs_vec_get_t(&pp.macros, box_shader_macro_t, i);
        ecs_os_free(m->name);
        ecs_os_free(m->value);
    }

    for (i = 0; i < ecs_vec_count(&pp.once); i ++) {
        ecs_os_free(*ecs_vec_get_t(&pp.once, char*, i));
    }

    ecs_vec_fini_t(NULL, &pp.macros, box_shader_macro_t);
    ecs_vec_fini_t(NULL, &pp.conds, box_shader_cond_t);
    ecs_vec_fini_t(NULL, &pp.stack, const char*);
    ecs_vec_fini_t(NULL, &pp.once, char*);

    return pp.failed ? -1 : 0;
}

void box_shader_output_fini(
    box_shader_output_t *out)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&out->files); i ++) {
        ecs_os_free(ecs_vec_get_t(&out->files, box_shader_file_t, i)->path);
    }

    ecs_vec_fini_t(NULL, &out->files, box_shader_file_t);
    for (i = 0; i < ecs_vec_count(&out->sources); i ++) {
        ecs_os_free(*ecs_vec_get_t(&out->sources, char*, i));
    }

    ecs_vec_fini_t(NULL, &out->sources, char*);
    ecs_vec_fini_t(NULL, &out->lines, box_shader_loc_t);
    ecs_os_free(out->code);
    ecs_os_free(out->error);
    out->code = NULL;
    out->error = NULL;
}
