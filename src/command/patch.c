/**
 * @file command/patch.c
 * @brief Applies a minimal unified diff to a text.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** One line of a text, without its line break */
typedef struct box_patch_line_t {
    const char *ptr;  /**< First character of the line */
    int32_t len;      /**< Characters before the line break */
} box_patch_line_t;

/** A patch that is being applied to a source text */
typedef struct box_patch_state_t {
    ecs_vec_t *src;     /**< Lines of the source text */
    ecs_strbuf_t *buf;  /**< Buffer that receives the patched text */
    int32_t cursor;     /**< Source line the next patch line applies to */
    int32_t hunk;       /**< Number of the hunk being applied */
    char *error;        /**< Message of the hunk that did not apply */
} box_patch_state_t;

/** Split a text into its lines, a trailing line break adds no empty line.
 *
 * @param text The text
 * @param lines Vector receiving the lines
 */
static void box_patch_split(
    const char *text,
    ecs_vec_t *lines)
{
    const char *p = text;
    while (p[0]) {
        const char *end = strchr(p, '\n');
        box_patch_line_t *line = ecs_vec_append_t(NULL, lines, box_patch_line_t);
        line->ptr = p;
        line->len = end ? (int32_t)(end - p) : (int32_t)strlen(p);
        if (!end) {
            break;
        }

        p = end + 1;
    }
}

/** Append a line and a line break to a buffer.
 *
 * @param buf The output buffer
 * @param ptr First character of the line
 * @param len Length of the line
 */
static void box_patch_emit(
    ecs_strbuf_t *buf,
    const char *ptr,
    int32_t len)
{
    ecs_strbuf_appendstrn(buf, ptr, len);
    ecs_strbuf_appendch(buf, '\n');
}

/** Parse the source range of an @@ hunk header.
 *
 * @param line The header line
 * @param start Receives the one based source line the hunk starts at
 * @param count Receives the number of source lines the hunk covers
 * @return 0 on success, -1 when the header is malformed
 */
static int box_patch_header(
    const box_patch_line_t *line,
    int32_t *start,
    int32_t *count)
{
    int result = -1;
    const char *p = line->ptr;
    char *end = NULL;
    long value = 0;
    if (line->len < 5 || p[2] != ' ' || p[3] != '-') {
        goto done;
    }

    p += 4;
    value = strtol(p, &end, 10);
    if (end == p) {
        goto done;
    }

    start[0] = (int32_t)value;
    count[0] = 1;
    if (end[0] == ',') {
        p = end + 1;
        value = strtol(p, &end, 10);
        if (end == p) {
            goto done;
        }

        count[0] = (int32_t)value;
    }

    result = 0;

done:
    return result;
}

/** Copy source lines to the output until a source line is reached.
 *
 * @param state The patch state
 * @param until Source line to stop at
 * @return 0 on success, -1 when the line is out of range
 */
static int box_patch_copy(
    box_patch_state_t *state,
    int32_t until)
{
    int result = 0;
    if (until < state->cursor || until > ecs_vec_count(state->src)) {
        state->error = flecs_asprintf(
            "hunk %d: does not apply at line %d", state->hunk, until + 1);
        result = -1;
        goto done;
    }

    while (state->cursor < until) {
        const box_patch_line_t *line = ecs_vec_get_t(
            state->src, box_patch_line_t, state->cursor);
        box_patch_emit(state->buf, line->ptr, line->len);
        state->cursor ++;
    }

done:
    return result;
}

/** Apply one context, addition or deletion line of a hunk.
 *
 * @param state The patch state
 * @param line The patch line
 * @return 0 on success, -1 when the line does not apply
 */
static int box_patch_line(
    box_patch_state_t *state,
    const box_patch_line_t *line)
{
    int result = 0;
    char kind = line->len ? line->ptr[0] : ' ';
    const char *text = line->len ? line->ptr + 1 : line->ptr;
    int32_t len = line->len ? line->len - 1 : 0;
    const box_patch_line_t *src = NULL;
    if (kind == '\\') {
        goto done;
    }

    if (kind == '+') {
        box_patch_emit(state->buf, text, len);
        goto done;
    }

    if (kind != ' ' && kind != '-') {
        state->error = flecs_asprintf(
            "hunk %d: unexpected patch line '%c'", state->hunk, kind);
        result = -1;
        goto done;
    }

    if (state->cursor < ecs_vec_count(state->src)) {
        src = ecs_vec_get_t(state->src, box_patch_line_t, state->cursor);
    }

    if (!src || src->len != len || ecs_os_memcmp(src->ptr, text, len)) {
        state->error = flecs_asprintf(
            "hunk %d: context does not match at line %d",
            state->hunk, state->cursor + 1);
        result = -1;
        goto done;
    }

    if (kind == ' ') {
        box_patch_emit(state->buf, src->ptr, src->len);
    }

    state->cursor ++;

done:
    return result;
}

/** Apply the header of a hunk: copy the source up to where the hunk starts.
 *
 * @param state The patch state
 * @param line The header line
 * @return 0 on success, -1 when the hunk does not apply
 */
static int box_patch_hunk(
    box_patch_state_t *state,
    const box_patch_line_t *line)
{
    int result = 0;
    int32_t start = 0, count = 0;
    state->hunk ++;
    if (box_patch_header(line, &start, &count)) {
        state->error = flecs_asprintf("hunk %d: invalid header", state->hunk);
        result = -1;
        goto done;
    }

    result = box_patch_copy(state, start > 0 ? start - 1 : 0);

done:
    return result;
}

char* box_patch_apply(
    const char *source,
    const char *patch,
    char **error)
{
    ecs_vec_t src, lines;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    box_patch_state_t state = { .src = &src, .buf = &buf };
    char *result = NULL;
    int32_t i, count, length;
    bool line_break = !source[0] || source[strlen(source) - 1] == '\n';
    ecs_vec_init_t(NULL, &src, box_patch_line_t, 0);
    ecs_vec_init_t(NULL, &lines, box_patch_line_t, 0);
    box_patch_split(source, &src);
    box_patch_split(patch, &lines);
    count = ecs_vec_count(&lines);

    for (i = 0; i < count; i ++) {
        const box_patch_line_t *line = ecs_vec_get_t(&lines, box_patch_line_t, i);
        if (line->len >= 2 && line->ptr[0] == '@' && line->ptr[1] == '@') {
            if (box_patch_hunk(&state, line)) {
                goto done;
            }
        } else if (state.hunk) {
            if (box_patch_line(&state, line)) {
                goto done;
            }
        }
    }

    if (!state.hunk) {
        state.error = ecs_os_strdup("patch contains no hunks");
        goto done;
    }

    if (box_patch_copy(&state, ecs_vec_count(&src))) {
        goto done;
    }

    result = ecs_strbuf_get(&buf);
    if (!result) {
        result = ecs_os_strdup("");
    }

    length = (int32_t)strlen(result);
    if (length && !line_break) {
        result[length - 1] = '\0';
    }

done:
    if (state.error) {
        ecs_strbuf_reset(&buf);
        if (error) {
            error[0] = state.error;
        } else {
            ecs_os_free(state.error);
        }
    }

    ecs_vec_fini_t(NULL, &src, box_patch_line_t);
    ecs_vec_fini_t(NULL, &lines, box_patch_line_t);

    return result;
}
