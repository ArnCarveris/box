/**
 * @file command/complete.c
 * @brief Completes the word a caret sits in against the commands and the entities.
 *
 * The first word of a line is a command, so it completes against the names
 * under box.command.commands, and every later word is an argument, so it
 * completes against the children of the scope its path names. Both answer the
 * same way: the longest prefix the candidates share replaces the word, a single
 * candidate is followed by a space and a word nothing matches is left alone.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** Compare two candidate strings.
 *
 * @param a The first candidate
 * @param b The second candidate
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_complete_compare(
    const void *a,
    const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/** Collect the dotted names of the commands of a scope into a vector.
 *
 * @param world The world
 * @param e The command or the group entity
 * @param out The vector of char*
 */
static void box_complete_command(
    ecs_world_t *world,
    ecs_entity_t e,
    void *out)
{
    if (!ecs_has(world, e, CommandInfo)) {
        box_hierarchy_children(world, e, box_complete_command, out);
    } else {
        ecs_vec_append_t(NULL, out, char*)[0] = ecs_get_path_w_sep(
            world, box_command_scope(world), e, ".", NULL);
    }
}

/** Collect the names of the children of a scope that start with a stem.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @param prefix Path of the scope with its trailing dot, empty for the root
 * @param stem Beginning of the name, empty for every child
 * @param out The vector of char*
 */
static void box_complete_children(
    ecs_world_t *world,
    ecs_entity_t scope,
    const char *prefix,
    const char *stem,
    ecs_vec_t *out)
{
    size_t length = strlen(stem);
    ecs_iter_t it = ecs_children(world, scope);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const char *name = ecs_get_name(world, it.entities[i]);
            if (!name || strncmp(name, stem, length)) {
                continue;
            }

            char *escaped = box_path_escape(name);
            ecs_vec_append_t(NULL, out, char*)[0] = flecs_asprintf(
                "%s%s", prefix, escaped);
            ecs_os_free(escaped);
        }
    }
}

/** Collect the entity paths that continue the word a caret sits in.
 *
 * @param world The world
 * @param session The session entity
 * @param word The word
 * @param out The vector of char*
 */
static void box_complete_entities(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *word,
    ecs_vec_t *out)
{
    const char *stem = strrchr(word, '.');
    ecs_entity_t scope = ecs_has(world, session, Terminal) ?
        box_path_cwd(world, session) : 0;
    char *prefix = ecs_os_strdup("");
    if (stem) {
        size_t size = (size_t)(stem - word) + 1;
        char *path = ecs_os_malloc((ecs_size_t)size + 1);

        ecs_os_memcpy(path, word, (ecs_size_t)size);
        path[size] = '\0';
        if (box_path_lookup(world, scope, path, &scope)) {
            ecs_os_free(path);
            goto done;
        }

        ecs_os_free(prefix);
        prefix = path;
        stem ++;
    } else {
        stem = word;
    }

    box_complete_children(world, scope, prefix, stem, out);
done:
    ecs_os_free(prefix);
}

/** Longest prefix that every candidate of a vector shares.
 *
 * @param candidates The vector of char*
 * @return Bytes of the shared prefix
 */
static int32_t box_complete_shared(
    const ecs_vec_t *candidates)
{
    const char *first = ecs_vec_get_t(candidates, char*, 0)[0];
    int32_t shared = (int32_t)strlen(first);
    int32_t i, count = ecs_vec_count(candidates);
    for (i = 1; i < count; i ++) {
        const char *other = ecs_vec_get_t(candidates, char*, i)[0];
        int32_t n = 0;

        while (n < shared && first[n] && first[n] == other[n]) {
            n ++;
        }

        shared = n;
    }

    return shared;
}

/** Release the candidates of a vector.
 *
 * @param candidates The vector of char*
 */
static void box_complete_free(
    ecs_vec_t *candidates)
{
    int32_t i, count = ecs_vec_count(candidates);
    for (i = 0; i < count; i ++) {
        ecs_os_free(ecs_vec_get_t(candidates, char*, i)[0]);
    }

    ecs_vec_fini_t(NULL, candidates, char*);
}

/** Write the candidates of a vector as one line each.
 *
 * @param candidates The vector of char*
 * @return The listing, the caller frees it
 */
static char* box_complete_listing(
    const ecs_vec_t *candidates)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(candidates);
    for (i = 0; i < count; i ++) {
        ecs_strbuf_append(&buf, "%s\n", ecs_vec_get_t(candidates, char*, i)[0]);
    }

    return ecs_strbuf_get(&buf);
}

/** Byte the word that ends at the caret starts at.
 *
 * @param line The command line
 * @param caret Byte the caret sits before
 * @return Byte the word starts at
 */
static int32_t box_complete_word(
    const char *line,
    int32_t caret)
{
    int32_t start = caret;
    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t') {
        start --;
    }

    return start;
}

/** Test whether the word that starts at a byte is the command of the line.
 *
 * @param line The command line
 * @param start Byte the word starts at
 * @return Whether every byte before the word is white space
 */
static bool box_complete_first(
    const char *line,
    int32_t start)
{
    bool result = true;
    int32_t i;
    for (i = 0; i < start; i ++) {
        if (line[i] != ' ' && line[i] != '\t') {
            result = false;
            break;
        }
    }

    return result;
}

char* box_command_complete(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *line,
    int32_t *caret_out,
    char **listing)
{
    char *result = NULL;
    ecs_vec_t candidates;
    ecs_vec_init_t(NULL, &candidates, char*, 0);

    if (listing) {
        listing[0] = NULL;
    }

    int32_t length = (int32_t)strlen(line);
    int32_t caret = caret_out ? caret_out[0] : length;
    if (caret < 0 || caret > length) {
        caret = length;
    }

    int32_t start = box_complete_word(line, caret);
    char *word = ecs_os_malloc(caret - start + 1);
    ecs_os_memcpy(word, &line[start], caret - start);
    word[caret - start] = '\0';

    if (box_complete_first(line, start)) {
        ecs_vec_t all;
        ecs_vec_init_t(NULL, &all, char*, 0);
        box_hierarchy_children(world, box_command_scope(world),
            box_complete_command, &all);

        size_t size = strlen(word);
        int32_t i, count = ecs_vec_count(&all);
        for (i = 0; i < count; i ++) {
            char *name = ecs_vec_get_t(&all, char*, i)[0];
            if (!strncmp(name, word, size)) {
                ecs_vec_append_t(NULL, &candidates, char*)[0] = name;
            } else {
                ecs_os_free(name);
            }
        }

        ecs_vec_fini_t(NULL, &all, char*);
    } else {
        box_complete_entities(world, session, word, &candidates);
    }

    int32_t count = ecs_vec_count(&candidates);
    if (!count) {
        goto done;
    }

    qsort(ecs_vec_first(&candidates), (size_t)count, sizeof(char*),
        box_complete_compare);

    int32_t shared = box_complete_shared(&candidates);
    const char *first = ecs_vec_get_t(&candidates, char*, 0)[0];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstrn(&buf, line, start);
    ecs_strbuf_appendstrn(&buf, first, shared);
    if (count == 1) {
        ecs_strbuf_appendch(&buf, ' ');
    }

    if (caret_out) {
        caret_out[0] = start + shared + (count == 1 ? 1 : 0);
    }

    ecs_strbuf_appendstr(&buf, &line[caret]);
    result = ecs_strbuf_get(&buf);
    if (listing && count > 1) {
        listing[0] = box_complete_listing(&candidates);
    }

done:
    box_complete_free(&candidates);
    ecs_os_free(word);

    return result;
}
