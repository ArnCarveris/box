/**
 * @file command/cmd_script.c
 * @brief The script_read, script_write, script_grep and script_patch commands.
 *
 * They operate on the scripts of the world that were loaded from a file inside the
 * working directory; a write goes through ecs_script_update and then to the file.
 */

#include "command.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Matches script_grep lists before it summarizes the rest */
#define BOX_SCRIPT_GREP_MAX (100)

/** A line of a text without its line ending */
typedef struct box_script_line_t {
    const char *ptr;    /**< First character of the line */
    int32_t len;        /**< Length of the line */
} box_script_line_t;

/** A hunk of a unified diff */
typedef struct box_script_hunk_t {
    int32_t start;      /**< Old line the hunk starts at, -1 when the header has none */
    int32_t first;      /**< Index of the first body line in the patch lines */
    int32_t last;       /**< Index after the last body line in the patch lines */
} box_script_hunk_t;

/** Get the working directory with its links resolved.
 *
 * @param out Receives the folder, PATH_MAX characters
 * @return 0 on success, -1 when the folder could not be determined
 */
static int box_script_root(
    char *out)
{
    char cwd[PATH_MAX];
    int result = -1;
    if (getcwd(cwd, sizeof(cwd)) && realpath(cwd, out)) {
        result = 0;
    }

    return result;
}

/** Make a path absolute and remove its ., .. and empty elements without touching the disk.
 *
 * @param root The working directory
 * @param path The path
 * @param out Receives the path, PATH_MAX characters
 * @return 0 on success, -1 when the path is too long
 */
static int box_script_normalize(
    const char *root,
    const char *path,
    char *out)
{
    char full[PATH_MAX];
    int32_t len = 0;
    int result = -1;
    int written = path[0] == '/' ? snprintf(full, sizeof(full), "%s", path)
        : snprintf(full, sizeof(full), "%s/%s", root, path);
    if (written < 0 || written >= PATH_MAX) {
        goto done;
    }

    const char *elem = full;
    while (elem[0]) {
        int32_t n = (int32_t)strcspn(elem, "/");
        if (n == 2 && !strncmp(elem, "..", 2)) {
            while (len && out[len - 1] != '/') {
                len --;
            }

            len -= len > 0;
        } else if (n && !(n == 1 && elem[0] == '.')) {
            len += snprintf(out + len, PATH_MAX - len, "/%.*s", n, elem);
        }

        if (len >= PATH_MAX) {
            goto done;
        }

        elem += n + (elem[n] == '/');
    }

    out[len] = '\0';
    if (!len) {
        ecs_os_strcpy(out, "/");
    }

    result = 0;
done:
    return result;
}

/** Resolve a path to an absolute path, following links when the file exists.
 *
 * @param root The working directory
 * @param path The path
 * @param out Receives the path, PATH_MAX characters
 * @return 1 when the file exists, 0 when it does not, -1 when the path is invalid
 */
static int box_script_resolve(
    const char *root,
    const char *path,
    char *out)
{
    int result = 1;
    if (!realpath(path, out)) {
        result = box_script_normalize(root, path, out);
    }

    return result;
}

/** Get a path relative to the working directory when it is inside it.
 *
 * @param root The working directory
 * @param path An absolute path
 * @return The path after the working directory, NULL when the path is outside it
 */
static const char* box_script_relative(
    const char *root,
    const char *path)
{
    size_t len = strlen(root);
    const char *result = NULL;
    if (!strncmp(path, root, len) && path[len] == '/') {
        result = path + len + 1;
    }

    return result;
}

/** Find the script of the world that was loaded from a file.
 *
 * @param world The world
 * @param path The file, relative to the working directory or absolute
 * @param file Receives the resolved file, PATH_MAX characters
 * @param exists Receives whether the file exists on disk
 * @param error Receives the reason when there is no script, which the caller frees
 * @return The script entity, 0 when there is none
 */
static ecs_entity_t box_script_find(
    ecs_world_t *world,
    const char *path,
    char *file,
    bool *exists,
    char **error)
{
    char root[PATH_MAX], have[PATH_MAX];
    ecs_entity_t result = 0;
    int resolved;
    if (box_script_root(root)) {
        error[0] = ecs_os_strdup("cannot read the working directory");
        goto done;
    }

    resolved = box_script_resolve(root, path, file);
    if (resolved < 0 || !box_script_relative(root, file)) {
        error[0] = flecs_asprintf("'%s' is outside the working directory", path);
        goto done;
    }

    exists[0] = resolved == 1;
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *s = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            if (result || !s[i].filename || s[i].template_) {
                continue;
            }

            if (box_script_resolve(root, s[i].filename, have) >= 0 && !strcmp(have, file)) {
                result = it.entities[i];
            }
        }
    }

    if (!result) {
        error[0] = flecs_asprintf("no script '%s'", path);
    }

done:
    return result;
}

/** Split a text into lines, dropping a carriage return before each line ending.
 *
 * @param text The text
 * @param lines Receives the lines, a vector of box_script_line_t
 * @return Whether the text ends with a line ending
 */
static bool box_script_lines(
    const char *text,
    ecs_vec_t *lines)
{
    const char *p = text;
    bool result = false;
    while (p[0]) {
        const char *end = strchr(p, '\n');
        int32_t len = end ? (int32_t)(end - p) : ecs_os_strlen(p);
        box_script_line_t *line = ecs_vec_append_t(NULL, lines, box_script_line_t);
        line->ptr = p;
        line->len = len - (len && p[len - 1] == '\r');
        result = end != NULL;
        p = end ? end + 1 : p + len;
    }

    return result;
}

/** Compare two lines, ignoring trailing whitespace.
 *
 * @param a The first line
 * @param a_len Length of the first line
 * @param b The second line
 * @param b_len Length of the second line
 * @return Whether the lines are equal
 */
static bool box_script_line_eq(
    const char *a,
    int32_t a_len,
    const char *b,
    int32_t b_len)
{
    while (a_len && (a[a_len - 1] == ' ' || a[a_len - 1] == '\t')) {
        a_len --;
    }

    while (b_len && (b[b_len - 1] == ' ' || b[b_len - 1] == '\t')) {
        b_len --;
    }

    return a_len == b_len && !strncmp(a, b, a_len);
}

/** Read the old line numbers of a hunk header such as "@@ -3,2 +3,4 @@".
 *
 * @param line The header
 * @param start Receives the old start line, -1 when the header has none
 * @param old_count Receives the old line count, -1 when the header has none
 * @param new_count Receives the new line count, -1 when the header has none
 */
static void box_script_hunk_header(
    const box_script_line_t *line,
    int32_t *start,
    int32_t *old_count,
    int32_t *new_count)
{
    char header[64] = {0};
    int32_t len = line->len < 63 ? line->len : 63;
    int a = 0, b = 1, c = 0, d = 1;
    ecs_os_memcpy(header, line->ptr, len);
    start[0] = old_count[0] = new_count[0] = -1;
    if (sscanf(header, "@@ -%d,%d +%d,%d", &a, &b, &c, &d) == 4 ||
        sscanf(header, "@@ -%d,%d +%d", &a, &b, &c) == 3)
    {
        start[0] = a;
        old_count[0] = b;
        new_count[0] = d;
    } else if (sscanf(header, "@@ -%d +%d,%d", &a, &c, &d) == 3 ||
        sscanf(header, "@@ -%d +%d", &a, &c) == 2)
    {
        start[0] = a;
        old_count[0] = 1;
        new_count[0] = d;
    }
}

/** Find the body lines of the hunk whose header is at a line of a patch.
 *
 * @param lines The patch lines
 * @param count Number of patch lines
 * @param at Index of the hunk header
 * @param out Receives the hunk
 * @return Index of the line after the hunk
 */
static int32_t box_script_hunk_parse(
    const box_script_line_t *lines,
    int32_t count,
    int32_t at,
    box_script_hunk_t *out)
{
    int32_t old_count, new_count, i = at + 1;
    bool counted;
    box_script_hunk_header(&lines[at], &out->start, &old_count, &new_count);
    counted = old_count >= 0;
    out->first = i;
    for (; i < count; i ++) {
        const box_script_line_t *l = &lines[i];
        char c = l->len ? l->ptr[0] : ' ';
        if (counted && old_count <= 0 && new_count <= 0 && c != '\\') {
            break;
        }

        if (l->len >= 2 && !strncmp(l->ptr, "@@", 2)) {
            break;
        }

        if (!strchr(" -+\\", c)) {
            break;
        }

        old_count -= c == ' ' || c == '-';
        new_count -= c == ' ' || c == '+';
    }

    out->last = i;

    return i;
}

/** Test whether the old lines of a hunk match the source at a line.
 *
 * @param src The source lines
 * @param src_count Number of source lines
 * @param patch The patch lines
 * @param hunk The hunk
 * @param at Index of the source line to compare the hunk with
 * @return Whether every context and removed line matches
 */
static bool box_script_hunk_matches(
    const box_script_line_t *src,
    int32_t src_count,
    const box_script_line_t *patch,
    const box_script_hunk_t *hunk,
    int32_t at)
{
    int32_t i;
    bool result = true;
    for (i = hunk->first; i < hunk->last && result; i ++) {
        const box_script_line_t *l = &patch[i];
        char c = l->len ? l->ptr[0] : ' ';
        if (c != ' ' && c != '-') {
            continue;
        }

        result = at < src_count && box_script_line_eq(src[at].ptr, src[at].len,
            l->ptr + (l->len > 0), l->len - (l->len > 0));
        at ++;
    }

    return result;
}

/** Count the context and removed lines of a hunk.
 *
 * @param patch The patch lines
 * @param hunk The hunk
 * @param prefix '-' to count the old lines, '+' to count the new lines
 * @return The number of lines
 */
static int32_t box_script_hunk_count(
    const box_script_line_t *patch,
    const box_script_hunk_t *hunk,
    char prefix)
{
    int32_t i, result = 0;
    for (i = hunk->first; i < hunk->last; i ++) {
        char c = patch[i].len ? patch[i].ptr[0] : ' ';
        result += c == ' ' || c == prefix;
    }

    return result;
}

/** Find where a hunk applies, closest to its header line and not before a line.
 *
 * @param src The source lines
 * @param src_count Number of source lines
 * @param patch The patch lines
 * @param hunk The hunk
 * @param from First source line the hunk may start at
 * @return Index of the source line, -1 when the hunk matches nowhere
 */
static int32_t box_script_hunk_find(
    const box_script_line_t *src,
    int32_t src_count,
    const box_script_line_t *patch,
    const box_script_hunk_t *hunk,
    int32_t from)
{
    int32_t old_count = box_script_hunk_count(patch, hunk, '-');
    int32_t expect = hunk->start - (old_count > 0);
    int32_t d, result = -1;
    expect = expect < from ? from : expect;
    expect = expect > src_count ? src_count : expect;
    for (d = 0; result < 0 && (expect + d <= src_count || expect - d >= from); d ++) {
        if (expect + d <= src_count &&
            box_script_hunk_matches(src, src_count, patch, hunk, expect + d))
        {
            result = expect + d;
        } else if (d && expect - d >= from &&
            box_script_hunk_matches(src, src_count, patch, hunk, expect - d))
        {
            result = expect - d;
        }
    }

    return result;
}

/** Append lines to a buffer, each followed by a line ending.
 *
 * @param buf The buffer
 * @param lines The lines
 * @param from Index of the first line
 * @param to Index after the last line
 */
static void box_script_append_lines(
    ecs_strbuf_t *buf,
    const box_script_line_t *lines,
    int32_t from,
    int32_t to)
{
    int32_t i;
    for (i = from; i < to; i ++) {
        ecs_strbuf_appendstrn(buf, lines[i].ptr, lines[i].len);
        ecs_strbuf_appendch(buf, '\n');
    }
}

/** Apply a unified diff to a text.
 *
 * A hunk applies at its header line or, when the lines moved, at the closest
 * place its context matches. Headers without line numbers search from the
 * previous hunk on.
 *
 * @param code The text
 * @param diff The unified diff
 * @param error Receives the reason when the diff does not apply, which the caller frees
 * @return The patched text which the caller frees, NULL when the diff does not apply
 */
static char* box_script_patch_apply(
    const char *code,
    const char *diff,
    char **error)
{
    ecs_vec_t src = {0}, patch = {0};
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char *result = NULL;
    bool newline = box_script_lines(code, &src) || !code[0];
    int32_t src_count, patch_count, i = 0, cursor = 0, hunks = 0;
    box_script_lines(diff, &patch);
    const box_script_line_t *s = ecs_vec_first(&src);
    const box_script_line_t *p = ecs_vec_first(&patch);
    src_count = ecs_vec_count(&src);
    patch_count = ecs_vec_count(&patch);
    while (i < patch_count) {
        box_script_hunk_t hunk;
        if (p[i].len < 2 || strncmp(p[i].ptr, "@@", 2)) {
            i ++;
            continue;
        }

        i = box_script_hunk_parse(p, patch_count, i, &hunk);
        hunks ++;
        int32_t at = box_script_hunk_find(s, src_count, p, &hunk, cursor);
        if (at < 0) {
            error[0] = flecs_asprintf("hunk %d does not apply", hunks);
            goto done;
        }

        box_script_append_lines(&buf, s, cursor, at);
        int32_t j;
        for (j = hunk.first; j < hunk.last; j ++) {
            char c = p[j].len ? p[j].ptr[0] : ' ';
            if (c == ' ' || c == '+') {
                ecs_strbuf_appendstrn(&buf, p[j].ptr + (p[j].len > 0), p[j].len - (p[j].len > 0));
                ecs_strbuf_appendch(&buf, '\n');
            }
        }

        cursor = at + box_script_hunk_count(p, &hunk, '-');
    }

    if (!hunks) {
        error[0] = ecs_os_strdup("patch has no hunks");
        goto done;
    }

    box_script_append_lines(&buf, s, cursor, src_count);
    result = ecs_strbuf_get(&buf);
    if (!result) {
        result = ecs_os_strdup("");
    }

    int32_t len = ecs_os_strlen(result);
    if (!newline && len && result[len - 1] == '\n') {
        result[len - 1] = '\0';
    }

done:
    if (!result) {
        ecs_strbuf_reset(&buf);
    }

    ecs_vec_fini_t(NULL, &src, box_script_line_t);
    ecs_vec_fini_t(NULL, &patch, box_script_line_t);

    return result;
}

/** Write a text to a file.
 *
 * @param file The file
 * @param code The text
 * @return 0 on success, -1 when the file could not be written
 */
static int box_script_save(
    const char *file,
    const char *code)
{
    int result = -1;
    FILE *f = fopen(file, "wb");
    if (!f) {
        goto done;
    }

    size_t len = strlen(code);
    result = fwrite(code, 1, len, f) == len ? 0 : -1;
    result |= fclose(f) ? -1 : 0;
done:
    return result;
}

/** Replace the code of a script, leaving it unchanged when the new code fails.
 *
 * The code is parsed before the world changes. When it fails to evaluate the
 * previous code is evaluated again. The file is written when it exists.
 *
 * @param world The world
 * @param script The script entity
 * @param file The resolved file of the script
 * @param exists Whether the file exists on disk
 * @param code The new code
 * @return NULL on success, the reason of the failure otherwise, which the caller frees
 */
static char* box_script_replace(
    ecs_world_t *world,
    ecs_entity_t script,
    const char *file,
    bool exists,
    const char *code)
{
    const EcsScript *s = ecs_get(world, script, EcsScript);
    ecs_script_eval_desc_t desc = { .skip_unknown = s->skip_unknown, .ir = s->ir };
    ecs_script_eval_result_t parse = {0};
    char *result = NULL, *old = NULL;
    ecs_script_t *parsed = ecs_script_parse(
        world, ecs_get_name(world, script), code, &desc, &parse);
    if (!parsed) {
        result = ecs_os_strdup(parse.error ? parse.error : "failed to parse script");
        goto done;
    }

    ecs_script_free(parsed);
    s = ecs_get(world, script, EcsScript);
    old = ecs_os_strdup(s->code ? s->code : "");
    if (ecs_script_update(world, script, 0, code)) {
        s = ecs_get(world, script, EcsScript);
        result = ecs_os_strdup(s && s->error ? s->error : "failed to evaluate script");
        ecs_script_update(world, script, 0, old);
        goto done;
    }

    if (exists && box_script_save(file, code)) {
        result = flecs_asprintf("world updated, but could not write '%s'", file);
    }

done:
    ecs_os_free(parse.error);
    ecs_os_free(old);

    return result;
}

/** List the files of the scripts in the working directory, one per line.
 *
 * @param world The world
 * @return The list, which the caller frees
 */
static char* box_script_list(
    ecs_world_t *world)
{
    char root[PATH_MAX], file[PATH_MAX];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char *result = NULL;
    if (box_script_root(root)) {
        goto done;
    }

    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *s = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const char *rel = NULL;
            if (s[i].filename && !s[i].template_ &&
                box_script_resolve(root, s[i].filename, file) >= 0)
            {
                rel = box_script_relative(root, file);
            }

            if (rel) {
                ecs_strbuf_append(&buf, "%s%s", ecs_strbuf_written(&buf) ? "\n" : "", rel);
            }
        }
    }

done:
    result = ecs_strbuf_get(&buf);

    return result ? result : ecs_os_strdup("");
}

/** The script_read command: print the code of a script, or list the scripts without a path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives the output
 */
static void box_script_read(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *path = *(char**)argv[1].ptr;
    char file[PATH_MAX], *error = NULL;
    bool exists = false;
    if (!path || !path[0]) {
        *(char**)result->ptr = box_script_list(ctx->world);
        goto done;
    }

    ecs_entity_t script = box_script_find(ctx->world, path, file, &exists, &error);
    if (!script) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    const EcsScript *s = ecs_get(ctx->world, script, EcsScript);
    *(char**)result->ptr = ecs_os_strdup(s->code ? s->code : "");
done:
    ecs_os_free(error);
}

/** The script_write command: replace the code of a script and its file.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the path and the code
 * @param result Receives the output
 */
static void box_script_write(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *path = *(char**)argv[1].ptr;
    const char *code = *(char**)argv[2].ptr;
    char file[PATH_MAX], *error = NULL;
    bool exists = false;
    ecs_entity_t script = box_script_find(ctx->world, path, file, &exists, &error);
    if (!script) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    error = box_script_replace(ctx->world, script, file, exists, code ? code : "");
    if (error) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    *(char**)result->ptr = ecs_os_strdup("ok");
done:
    ecs_os_free(error);
}

/** Append the lines of a script that contain a text as file:line:text.
 *
 * @param buf The output buffer
 * @param file The file shown in front of the line number
 * @param code The code of the script
 * @param text The text to look for
 * @param matches The matches so far, incremented for each matching line
 */
static void box_script_grep_lines(
    ecs_strbuf_t *buf,
    const char *file,
    const char *code,
    const char *text,
    int32_t *matches)
{
    ecs_vec_t lines = {0};
    int32_t i, text_len = ecs_os_strlen(text);
    box_script_lines(code, &lines);
    const box_script_line_t *l = ecs_vec_first(&lines);
    for (i = 0; i < ecs_vec_count(&lines); i ++) {
        const char *p = l[i].ptr, *end = l[i].ptr + l[i].len;
        while (p + text_len <= end && strncmp(p, text, text_len)) {
            p ++;
        }

        if (p + text_len > end) {
            continue;
        }

        if (matches[0] < BOX_SCRIPT_GREP_MAX) {
            ecs_strbuf_append(buf, "%s%s:%d:", ecs_strbuf_written(buf) ? "\n" : "", file, i + 1);
            ecs_strbuf_appendstrn(buf, l[i].ptr, l[i].len);
        }

        matches[0] ++;
    }

    ecs_vec_fini_t(NULL, &lines, box_script_line_t);
}

/** The script_grep command: print the lines of the scripts that contain a text.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the text
 * @param result Receives the output
 */
static void box_script_grep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *text = *(char**)argv[1].ptr;
    char root[PATH_MAX], file[PATH_MAX];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t matches = 0;
    if (box_script_root(root)) {
        box_command_error(ctx, result, "cannot read the working directory");
        goto done;
    }

    ecs_iter_t it = ecs_each(ctx->world, EcsScript);
    while (ecs_each_next(&it)) {
        const EcsScript *s = ecs_field(&it, EcsScript, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const char *rel = NULL;
            if (s[i].filename && s[i].code && !s[i].template_ &&
                box_script_resolve(root, s[i].filename, file) >= 0)
            {
                rel = box_script_relative(root, file);
            }

            if (rel) {
                box_script_grep_lines(&buf, rel, s[i].code, text ? text : "", &matches);
            }
        }
    }

    if (matches > BOX_SCRIPT_GREP_MAX) {
        ecs_strbuf_append(&buf, "\n... %d more matches", matches - BOX_SCRIPT_GREP_MAX);
    }

    char *out = ecs_strbuf_get(&buf);
    *(char**)result->ptr = out ? out : ecs_os_strdup("");
done:
    return;
}

/** The script_patch command: apply a unified diff to a script and its file.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the path and the diff
 * @param result Receives the output
 */
static void box_script_patch(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *path = *(char**)argv[1].ptr;
    const char *diff = *(char**)argv[2].ptr;
    char file[PATH_MAX], *error = NULL, *patched = NULL;
    bool exists = false;
    ecs_entity_t script = box_script_find(ctx->world, path, file, &exists, &error);
    if (!script) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    const EcsScript *s = ecs_get(ctx->world, script, EcsScript);
    patched = box_script_patch_apply(s->code ? s->code : "", diff ? diff : "", &error);
    if (!patched) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    error = box_script_replace(ctx->world, script, file, exists, patched);
    if (error) {
        box_command_error(ctx, result, "%s", error);
        goto done;
    }

    *(char**)result->ptr = ecs_os_strdup("ok");
done:
    ecs_os_free(error);
    ecs_os_free(patched);
}

void box_cmd_script_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "script_read",
        .brief = "Print the code of a script file, list the script files without a path",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t), .optional = true }},
        .callback = box_script_read
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "script_write",
        .brief = "Replace the code of a script file and update the world",
        .params = {
            { .name = "path", .type = ecs_id(ecs_string_t) },
            { .name = "code", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = box_script_write
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "script_grep",
        .brief = "Print the lines of the script files that contain a text",
        .params = {{ .name = "text", .type = ecs_id(ecs_string_t), .rest = true }},
        .callback = box_script_grep
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "script_patch",
        .brief = "Apply a unified diff to a script file and update the world",
        .params = {
            { .name = "path", .type = ecs_id(ecs_string_t) },
            { .name = "patch", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = box_script_patch
    });
}
