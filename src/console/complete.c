/**
 * @file console/complete.c
 * @brief Tab completion of the input line: command names, help topics and entity arguments,
 * like the completion of the flecs explorer terminal.
 */

#include "console.h"
#include <stdlib.h>
#include <string.h>

/** Name of the help command, whose first argument completes to command names */
#define BOX_CONSOLE_COMPLETE_HELP "help"

/** Description of the help group of the console commands in the candidate list */
#define BOX_CONSOLE_COMPLETE_GROUP_BRIEF "Help group of the commands only the console runs"

/** Free the text of a candidate.
 *
 * @param c The candidate
 */
static void box_console_candidate_fini(
    box_console_candidate_t *c)
{
    ecs_os_free(c->text);
    ecs_os_free(c->label);
    ecs_os_free(c->brief);
}

/** Forget the candidates.
 *
 * @param cc The completion state
 */
static void box_console_complete_clear(
    box_console_complete_t *cc)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&cc->candidates); i ++) {
        box_console_candidate_fini(ecs_vec_get_t(&cc->candidates, box_console_candidate_t, i));
    }

    ecs_vec_clear(&cc->candidates);
    cc->capped = false;
}

void box_console_complete_close(
    box_console_t *con)
{
    box_console_complete_t *cc = &con->complete;
    con->refresh = con->refresh || cc->open;
    box_console_complete_clear(cc);
    cc->open = false;
    cc->active = -1;
}

/** Add a candidate, unless the completion collected BOX_CONSOLE_COMPLETE_MAX already.
 *
 * @param cc The completion state
 * @param text Text that replaces the word, taken over by the candidate
 * @param label Text the list shows
 * @param brief Description, NULL for none
 * @param kind Color of the label
 * @param entity Entity of the candidate, 0 for none
 */
static void box_console_candidate_add(
    box_console_complete_t *cc,
    char *text,
    const char *label,
    const char *brief,
    box_console_syntax_t kind,
    ecs_entity_t entity)
{
    if (ecs_vec_count(&cc->candidates) >= BOX_CONSOLE_COMPLETE_MAX) {
        cc->capped = true;
        ecs_os_free(text);
    } else {
        *ecs_vec_append_t(NULL, &cc->candidates, box_console_candidate_t) =
            (box_console_candidate_t){
                .text = text,
                .label = ecs_os_strdup(label),
                .brief = brief && brief[0] ? ecs_os_strdup(brief) : NULL,
                .kind = kind,
                .entity = entity
            };
    }
}

/** Test whether a candidate with a text was collected already.
 *
 * @param cc The completion state
 * @param text The text
 * @return Whether a candidate has the text
 */
static bool box_console_candidate_has(
    const box_console_complete_t *cc,
    const char *text)
{
    int32_t i;
    bool result = false;
    for (i = 0; i < ecs_vec_count(&cc->candidates) && !result; i ++) {
        result = !strcmp(ecs_vec_get_t(&cc->candidates, box_console_candidate_t, i)->text,
            text);
    }

    return result;
}

/** Add a command name that starts with the prefix and was not added yet.
 *
 * @param cc The completion state
 * @param name The command name
 * @param brief Its description, NULL for none
 * @param kind Color of the name
 * @param prefix The word typed so far
 */
static void box_console_complete_name(
    box_console_complete_t *cc,
    const char *name,
    const char *brief,
    box_console_syntax_t kind,
    const char *prefix)
{
    if (name && !strncmp(name, prefix, strlen(prefix)) && !box_console_candidate_has(cc, name)) {
        box_console_candidate_add(cc, ecs_os_strdup(name), name, brief, kind, 0);
    }
}

/** Collect the command names that start with the word: the commands in the scopes of
 * EcsScriptShell.command_paths, the first of a name as the shell runs it, and the console
 * commands, with the console help group for a help topic.
 *
 * @param world The world
 * @param cc The completion state
 * @param prefix The word typed so far
 * @param topic Whether the word is the topic of help
 */
static void box_console_complete_commands(
    const ecs_world_t *world,
    box_console_complete_t *cc,
    const char *prefix,
    bool topic)
{
    const EcsScriptShell *shell = ecs_singleton_get(world, EcsScriptShell);
    const ecs_entity_t *paths = shell ? ecs_vec_first(&shell->command_paths) : NULL;
    int32_t i, p, count = shell ? ecs_vec_count(&shell->command_paths) : 0;
    const char *name, *brief;
    for (p = 0; p < count; p ++) {
        ecs_iter_t it;
        if (!paths[p] || !ecs_is_alive(world, paths[p])) {
            continue;
        }

        it = ecs_children(world, paths[p]);
        while (ecs_children_next(&it)) {
            for (i = 0; i < it.count; i ++) {
                ecs_entity_t f = it.entities[i];
                if (ecs_has(world, f, EcsScriptFunction)) {
                    box_console_complete_name(cc, ecs_get_name(world, f),
                        ecs_doc_get_brief(world, f), BoxOutputCommand, prefix);
                }
            }
        }
    }

    for (i = 0; (name = box_console_command_at(i, &brief)) != NULL; i ++) {
        box_console_complete_name(cc, name, brief, BoxOutputCommand, prefix);
    }

    if (topic) {
        box_console_complete_name(cc, BOX_CONSOLE_HELP_GROUP, BOX_CONSOLE_COMPLETE_GROUP_BRIEF,
            BoxSyntaxScope, prefix);
    }
}

/** Get the color of an entity candidate: module, component or entity.
 *
 * @param world The world
 * @param e The entity
 * @return What the candidate is
 */
static box_console_syntax_t box_console_complete_kind(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    box_console_syntax_t result = BoxOutputEntity;
    if (ecs_has_id(world, e, EcsModule)) {
        result = BoxSyntaxScope;
    } else if (ecs_has(world, e, EcsComponent)) {
        result = BoxOutputComponent;
    }

    return result;
}

/** Collect the children of one parent whose name starts with the last part of the word.
 *
 * Without a parent part the parent is the scope of the session. A dotted word (a.b.c) lists the
 * children of its parent part, resolved as the shell resolves it; a relative word (./a, ../a)
 * the children of the part before its last /.
 *
 * @param world The world
 * @param con The console state
 * @param cc The completion state
 * @param word The word typed so far
 */
static void box_console_complete_entities(
    ecs_world_t *world,
    box_console_t *con,
    box_console_complete_t *cc,
    const char *word)
{
    ecs_entity_t scope = box_console_session_scope(world, con), parent = scope;
    bool relative = word[0] == '.', found = true;
    const char *sep = strrchr(word, relative ? '/' : '.');
    int32_t head = sep ? (int32_t)(sep - word) + 1 : 0;
    const char *prefix = &word[head];
    size_t len = strlen(prefix);
    ecs_iter_t it;
    int32_t i;
    if (relative && !sep) {
        goto done;
    }

    if (sep) {
        char *path = flecs_asprintf("%.*s", head - 1, word);
        parent = path[0] ? box_console_syntax_resolve(world, scope, path, &found) : 0;
        ecs_os_free(path);
    }

    if (!found) {
        goto done;
    }

    it = ecs_children(world, parent);
    while (!cc->capped && ecs_children_next(&it)) {
        for (i = 0; i < it.count && !cc->capped; i ++) {
            ecs_entity_t e = it.entities[i];
            const char *name = ecs_get_name(world, e);
            if (!name || (!parent && ecs_get_parent(world, e)) || strncmp(name, prefix, len)) {
                continue;
            }

            box_console_candidate_add(cc, flecs_asprintf("%.*s%s", head, word, name), name,
                NULL, box_console_complete_kind(world, e), e);
        }
    }

    if (cc->capped) {
        ecs_iter_fini(&it);
    }

done:
    return;
}

/** Test whether a parameter takes an entity, like isEntityParam of the explorer terminal: its
 * type is flecs.meta.entity or flecs.meta.id, or it is named script.
 *
 * @param param The parameter
 * @return Whether its argument completes to entity names
 */
static bool box_console_complete_entity_param(
    const ecs_script_parameter_t *param)
{
    return param->type == ecs_id(ecs_entity_t) || param->type == ecs_id(ecs_id_t) ||
        (param->name && !strcmp(param->name, "script"));
}

/** Order candidates by their text.
 *
 * @param a The first candidate
 * @param b The second candidate
 * @return Less than, equal to or greater than zero
 */
static int box_console_candidate_cmp(
    const void *a,
    const void *b)
{
    return strcmp(((const box_console_candidate_t*)a)->text,
        ((const box_console_candidate_t*)b)->text);
}

/** Collect the candidates of the word under the cursor, from the parameters of its command.
 *
 * @param world The world
 * @param con The console state
 * @param w The word
 */
static void box_console_complete_collect(
    ecs_world_t *world,
    box_console_t *con,
    const box_console_word_t *w)
{
    box_console_complete_t *cc = &con->complete;
    char *word = flecs_asprintf("%.*s", w->end - w->start, &con->line[w->start]);
    ecs_entity_t cmd = 0;
    const EcsScriptFunction *f = NULL;
    const ecs_script_parameter_t *param = NULL;
    box_console_complete_clear(cc);
    if (!w->index) {
        box_console_complete_commands(world, cc, word, false);
    } else if (w->index == 1 && !strcmp(w->command, BOX_CONSOLE_COMPLETE_HELP)) {
        box_console_complete_commands(world, cc, word, true);
    } else if (!box_console_is_command(w->command, (int32_t)strlen(w->command))) {
        cmd = box_console_syntax_command(world, w->command);
        f = cmd ? ecs_get(world, cmd, EcsScriptFunction) : NULL;
    }

    if (f && w->index <= ecs_vec_count(&f->params)) {
        param = ecs_vec_get_t(&f->params, ecs_script_parameter_t, w->index - 1);
    }

    if (param && box_console_complete_entity_param(param)) {
        box_console_complete_entities(world, con, cc, word);
    }

    if (ecs_vec_count(&cc->candidates) > 1) {
        qsort(ecs_vec_first(&cc->candidates), (size_t)ecs_vec_count(&cc->candidates),
            sizeof(box_console_candidate_t), box_console_candidate_cmp);
    }

    ecs_os_free(word);
}

/** Get the length of the prefix all candidates share, like commonPrefix of the explorer
 * terminal, kept at the start of a character.
 *
 * @param cc The completion state
 * @return Bytes of the common prefix
 */
static int32_t box_console_complete_common(
    const box_console_complete_t *cc)
{
    const box_console_candidate_t *c = ecs_vec_first(&cc->candidates);
    int32_t i, j, count = ecs_vec_count(&cc->candidates);
    int32_t result = count ? (int32_t)strlen(c[0].text) : 0;
    for (i = 1; i < count; i ++) {
        for (j = 0; j < result && c[i].text[j] == c[0].text[j]; j ++) { }
        result = j;
    }

    while (result > 0 && ((unsigned char)c[0].text[result] & 0xC0) == 0x80) {
        result --;
    }

    return result;
}

/** Test whether an entity has a child, reading only the first table of its children.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it has children
 */
static bool box_console_complete_children(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    if (e && ecs_is_alive(world, e)) {
        ecs_iter_t it = ecs_children(world, e);
        while (!result && ecs_children_next(&it)) {
            result = it.count > 0;
        }

        if (result) {
            ecs_iter_fini(&it);
        }
    }

    return result;
}

/** Put a candidate in the line in place of the word, followed by a space, or by the separator
 * of its path for an entity that has children, so the next Tab completes them.
 *
 * @param world The world
 * @param con The console state
 * @param c The candidate
 */
static void box_console_complete_finish(
    ecs_world_t *world,
    box_console_t *con,
    const box_console_candidate_t *c)
{
    const char *suffix = " ";
    char *text;
    if (box_console_complete_children(world, c->entity)) {
        suffix = c->text[0] == '.' ? "/" : ".";
    }

    text = flecs_asprintf("%s%s", c->text, suffix);
    box_console_replace(con, con->complete.start, con->complete.end, text);
    ecs_os_free(text);
}

/** Flash the prompt: Tab found nothing to complete.
 *
 * @param con The console state
 */
static void box_console_complete_bell(
    box_console_t *con)
{
    con->complete.bell = BOX_CONSOLE_BELL;
    con->refresh = true;
}

/** Move the active candidate of the open list one step and put it in the line.
 *
 * @param con The console state
 * @param back Whether to step backward
 */
static void box_console_complete_cycle(
    box_console_t *con,
    bool back)
{
    box_console_complete_t *cc = &con->complete;
    int32_t count = ecs_vec_count(&cc->candidates);
    const box_console_candidate_t *c;
    if (cc->active < 0) {
        cc->active = back ? count - 1 : 0;
    } else {
        cc->active = (cc->active + (back ? count - 1 : 1)) % count;
    }

    c = ecs_vec_get_t(&cc->candidates, box_console_candidate_t, cc->active);
    box_console_replace(con, cc->start, cc->end, c->text);
    cc->end = cc->start + (int32_t)strlen(c->text);
    con->refresh = true;
}

void box_console_complete_tab(
    ecs_world_t *world,
    box_console_t *con,
    bool back)
{
    box_console_complete_t *cc = &con->complete;
    box_console_word_t w;
    int32_t count, common;
    char *prefix;
    if (cc->open && ecs_vec_count(&cc->candidates)) {
        box_console_complete_cycle(con, back);
        goto done;
    }

    box_console_complete_close(con);
    if (!box_console_syntax_word_at(con->line, con->cursor, &w)) {
        box_console_complete_bell(con);
        goto done;
    }

    box_console_complete_collect(world, con, &w);
    count = ecs_vec_count(&cc->candidates);
    cc->start = w.start;
    cc->end = w.end;
    common = count && !cc->capped ? box_console_complete_common(cc) : w.end - w.start;
    if (!count) {
        box_console_complete_bell(con);
    } else if (count == 1 && !cc->capped) {
        box_console_complete_finish(world, con, ecs_vec_first(&cc->candidates));
        box_console_complete_clear(cc);
    } else if (common > w.end - w.start) {
        prefix = flecs_asprintf("%.*s", common,
            ecs_vec_first_t(&cc->candidates, box_console_candidate_t)->text);
        box_console_replace(con, cc->start, cc->end, prefix);
        ecs_os_free(prefix);
        box_console_complete_clear(cc);
    } else {
        cc->open = true;
        cc->active = -1;
        con->refresh = true;
    }

    ecs_os_free(w.command);
done:
    return;
}

bool box_console_complete_accept(
    ecs_world_t *world,
    box_console_t *con)
{
    box_console_complete_t *cc = &con->complete;
    bool result = cc->open && cc->active >= 0 && cc->active < ecs_vec_count(&cc->candidates);
    if (result) {
        box_console_complete_finish(world, con,
            ecs_vec_get_t(&cc->candidates, box_console_candidate_t, cc->active));
        box_console_complete_close(con);
    }

    return result;
}
