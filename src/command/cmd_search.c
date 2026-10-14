/**
 * @file command/cmd_search.c
 * @brief The query, iter, next, done, has, grep and grep_recur commands.
 *
 * Named queries are child entities of the session. Iteration re-runs the query and skips the
 * rows already returned, so no iterator is kept open between commands.
 */

#include "command.h"
#include <stdlib.h>
#include <string.h>

/** Lines grep prints before the output is cut off */
#define BOX_SEARCH_LIMIT (100)

/** Characters of a value grep prints on each side of a match */
#define BOX_SEARCH_CONTEXT (40)

/** Iteration state of a terminal session */
typedef struct SearchIter {
    ecs_entity_t query; /**< Query entity being iterated, 0 when not iterating */
    int32_t offset;     /**< Rows returned so far */
    int32_t mark;       /**< Lines the session had logged when iter or next last ran */
} SearchIter;

/** An entity grep searches, with the path it is sorted and printed by */
typedef struct box_search_entity_t {
    ecs_entity_t e;     /**< The entity */
    char *path;         /**< Path of the entity */
} box_search_entity_t;

/** Output of a grep */
typedef struct box_search_out_t {
    ecs_strbuf_t buf;   /**< The printed hits */
    int32_t count;      /**< Hits found, including the ones that were not printed */
} box_search_out_t;

ECS_COMPONENT_DECLARE(SearchIter);

/** Get the number of lines a session has logged.
 *
 * @param world The world
 * @param session The session entity
 * @return The number of logged lines
 */
static int32_t box_search_logged(
    ecs_world_t *world,
    ecs_entity_t session)
{
    const TerminalSession *s = ecs_get(world, session, TerminalSession);
    int32_t result = 0;
    if (s) {
        result = ecs_vec_count(&s->entries);
    }

    return result;
}

/** Find a named query of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param name The query name
 * @return The query entity, 0 when the session has no query with that name
 */
static ecs_entity_t box_search_find(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *name)
{
    ecs_entity_t result = ecs_lookup_child(world, session, name);
    if (result && !ecs_query_get(world, result)) {
        result = 0;
    }

    return result;
}

/** Create a query from an expression without logging parse errors.
 *
 * @param world The world
 * @param entity The entity that owns the query, 0 for a query without one
 * @param expr The query expression
 * @return The query, NULL when the expression is invalid
 */
static ecs_query_t* box_search_parse(
    ecs_world_t *world,
    ecs_entity_t entity,
    const char *expr)
{
    int prev = ecs_log_set_level(-4);
    ecs_query_t *result = ecs_query(world, { .entity = entity, .expr = expr });
    ecs_log_set_level(prev);

    return result;
}

/** Append the value a field of an iterator has for a row.
 *
 * @param it The iterator
 * @param field The field
 * @param row The row
 * @param buf The buffer
 */
static void box_search_field(
    const ecs_iter_t *it,
    int8_t field,
    int32_t row,
    ecs_strbuf_t *buf)
{
    size_t size = ecs_field_is_set(it, field) ? ecs_field_size(it, field) : 0;
    ecs_entity_t type = size ? ecs_get_typeid(it->world, ecs_field_id(it, field)) : 0;
    void *ptr = NULL;
    char *value = NULL;
    if (!type || !ecs_has(it->world, type, EcsTypeSerializer)) {
        goto done;
    }

    if (it->row_fields & (1llu << field)) {
        ptr = ecs_field_at_w_size(it, size, field, row);
    } else {
        ptr = ecs_field_w_size(it, size, field);
        if (ptr && ecs_field_is_self(it, field)) {
            ptr = ECS_ELEM(ptr, (ecs_size_t)size, row);
        }
    }

    value = ptr ? ecs_ptr_to_expr(it->world, type, ptr) : NULL;
    if (value) {
        ecs_strbuf_append(buf, " %s", value);
    }

done:
    ecs_os_free(value);
}

/** Append a row of an iterator as the entity path followed by its field values.
 *
 * @param it The iterator
 * @param row The row
 * @param buf The buffer
 */
static void box_search_row(
    const ecs_iter_t *it,
    int32_t row,
    ecs_strbuf_t *buf)
{
    int8_t i;
    if (ecs_strbuf_written(buf)) {
        ecs_strbuf_appendch(buf, '\n');
    }

    if (it->entities) {
        char *path = ecs_get_path(it->world, it->entities[row]);
        ecs_strbuf_appendstr(buf, path);
        ecs_os_free(path);
    } else {
        ecs_strbuf_appendch(buf, '_');
    }

    for (i = 0; i < it->field_count; i ++) {
        box_search_field(it, i, row, buf);
    }
}

/** Run a query and append count rows from the first one on.
 *
 * A result without entities counts as one row.
 *
 * @param world The world
 * @param q The query
 * @param first The first row to append
 * @param count Rows to append, -1 to only count them all
 * @param buf The buffer, NULL to only count
 * @return Rows seen, which stops one past first + count when count is not -1
 */
static int32_t box_search_rows(
    ecs_world_t *world,
    const ecs_query_t *q,
    int32_t first,
    int32_t count,
    ecs_strbuf_t *buf)
{
    ecs_iter_t it = ecs_query_iter(world, q);
    int32_t result = 0, i, rows;
    bool more = true;
    while (more && ecs_query_next(&it)) {
        rows = it.count ? it.count : (it.entities ? 0 : 1);
        for (i = 0; i < rows && more; i ++) {
            if (buf && result >= first && result < first + count) {
                box_search_row(&it, i, buf);
            }

            result ++;
            more = count < 0 || result <= first + count;
        }
    }

    if (!more) {
        ecs_iter_fini(&it);
    }

    return result;
}

/** Append the number of rows of a query.
 *
 * @param world The world
 * @param q The query
 * @return The count, which the caller frees
 */
static char* box_search_count(
    ecs_world_t *world,
    const ecs_query_t *q)
{
    int32_t count = box_search_rows(world, q, 0, -1, NULL);

    return flecs_asprintf("%d result%s", count, count == 1 ? "" : "s");
}

/** List the named queries of a session as name: expression.
 *
 * @param world The world
 * @param session The session entity
 * @return The list, which the caller frees
 */
static char* box_search_list(
    ecs_world_t *world,
    ecs_entity_t session)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_iter_t it = ecs_children(world, session);
    int32_t i;
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const char *expr = ecs_doc_get_brief(world, e);
            if (!ecs_query_get(world, e)) {
                continue;
            }

            if (ecs_strbuf_written(&buf)) {
                ecs_strbuf_appendch(&buf, '\n');
            }

            ecs_strbuf_append(&buf, "%s: %s", ecs_get_name(world, e), expr ? expr : "");
        }
    }

    return ecs_strbuf_get(&buf);
}

/** The query command: create or replace a named query of the session.
 *
 * Without an expression it prints the expression of the query, without a name the list
 * of queries.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the name and the expression
 * @param result Receives the output
 */
static void box_search_query(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *name = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0, existing;
    ecs_query_t *q;
    if (!name) {
        *(char**)result->ptr = box_search_list(world, session);
        goto done;
    }

    existing = box_search_find(world, session, name);
    if (!expr) {
        if (existing) {
            *(char**)result->ptr = ecs_os_strdup(ecs_doc_get_brief(world, existing));
        } else {
            box_command_error(ctx, result, "unknown query '%s'", name);
        }

        goto done;
    }

    if (strchr(name, '.') || (!existing && ecs_lookup_child(world, session, name))) {
        box_command_error(ctx, result, "invalid query name '%s'", name);
        goto done;
    }

    if (existing) {
        ecs_delete(world, existing);
    }

    e = ecs_entity(world, { .name = name, .parent = session });
    q = box_search_parse(world, e, expr);
    if (!q) {
        ecs_delete(world, e);
        box_command_error(ctx, result, "invalid query '%s'", expr);
        goto done;
    }

    ecs_doc_set_brief(world, e, expr);
    *(char**)result->ptr = box_search_count(world, q);
done:
    return;
}

/** The iter command: start iterating a named query of the session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the query name
 * @param result Receives the number of results
 */
static void box_search_iter(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *name = *(char**)argv[1].ptr;
    ecs_entity_t e = box_search_find(world, session, name);
    if (!e) {
        ecs_remove(world, session, SearchIter);
        box_command_error(ctx, result, "unknown query '%s'", name);
        goto done;
    }

    ecs_set(world, session, SearchIter, {
        .query = e,
        .mark = box_search_logged(world, session)
    });

    *(char**)result->ptr = box_search_count(world, ecs_query_get(world, e));
done:
    return;
}

/** The next command: print the next count rows of the iterated query, one when omitted.
 *
 * The iteration ends when another command ran since the last iter or next, and when the
 * query has no rows left, which the output marks with (end).
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the count
 * @param result Receives the rows
 */
static void box_search_next(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    int32_t count = *(ecs_i32_t*)argv[1].ptr;
    int32_t logged = box_search_logged(world, session);
    SearchIter *st = ecs_get_mut(world, session, SearchIter);
    const ecs_query_t *q = NULL;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t seen;
    if (st && st->query && logged - st->mark <= 1 && ecs_is_alive(world, st->query)) {
        q = ecs_query_get(world, st->query);
    }

    if (!q) {
        ecs_remove(world, session, SearchIter);
        box_command_error(ctx, result, "not iterating, use iter first");
        goto done;
    }

    count = count > 0 ? count : 1;
    seen = box_search_rows(world, q, st->offset, count, &buf);
    st->offset = seen < st->offset + count ? seen : st->offset + count;
    st->mark = logged;
    if (seen <= st->offset) {
        ecs_strbuf_appendstr(&buf, ecs_strbuf_written(&buf) ? "\n(end)" : "(end)");
        ecs_remove(world, session, SearchIter);
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

/** The done command: stop iterating.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_search_done(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)result;
    ecs_remove(ctx->world, *(ecs_entity_t*)argv[0].ptr, SearchIter);
}

/** The has command: test whether an entity has a component or matches a query.
 *
 * The query is a named query of the session or a query expression.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the entity and the component or query
 * @param result Receives true or false
 */
static void box_search_has(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    ecs_entity_t e = *(ecs_entity_t*)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t named = box_search_find(world, session, expr);
    ecs_query_t *temp = NULL;
    const ecs_query_t *q;
    ecs_iter_t it;
    bool has;
    if (named) {
        q = ecs_query_get(world, named);
    } else {
        q = temp = box_search_parse(world, 0, expr);
    }

    if (!q) {
        box_command_error(ctx, result, "invalid query '%s'", expr);
        goto done;
    }

    has = ecs_query_has(q, e, &it);
    if (has) {
        ecs_iter_fini(&it);
    }

    *(char**)result->ptr = ecs_os_strdup(has ? "true" : "false");
done:
    if (temp) {
        ecs_query_fini(temp);
    }
}

/** Append an entity to the entities grep searches.
 *
 * @param world The world
 * @param e The entity
 * @param out The vector of box_search_entity_t
 */
static void box_search_add(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out)
{
    box_search_entity_t *entry = ecs_vec_append_t(NULL, out, box_search_entity_t);
    entry->e = e;
    entry->path = ecs_get_path(world, e);
}

/** Append the named entities without a parent.
 *
 * @param world The world
 * @param out The vector of box_search_entity_t
 */
static void box_search_add_root(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    int32_t i;
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_pair(ecs_id(EcsIdentifier), EcsName) },
            { .id = ecs_pair(EcsChildOf, EcsWildcard), .oper = EcsNot },
            { .id = ecs_id(EcsParent), .oper = EcsNot }
        }
    });

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_search_add(world, it.entities[i], out);
        }
    }

    ecs_query_fini(q);
}

/** Append the children of an entity, none for a wildcard.
 *
 * @param world The world
 * @param parent The parent
 * @param out The vector of box_search_entity_t
 */
static void box_search_add_children(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_vec_t *out)
{
    int32_t i;
    ecs_iter_t it;
    if (ecs_id_is_wildcard(parent)) {
        goto done;
    }

    it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_search_add(world, it.entities[i], out);
        }
    }

done:
    return;
}

/** Compare two grep entities by path.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative, zero or positive as a sorts before, with or after b
 */
static int box_search_compare(
    const void *a,
    const void *b)
{
    const box_search_entity_t *ea = a;
    const box_search_entity_t *eb = b;

    return strcmp(ea->path, eb->path);
}

/** Append a grep hit, counting the ones past the limit without printing them.
 *
 * @param out The output
 * @param path Path of the entity
 * @param id The component, NULL for a match of the entity name
 * @param value The matching value, NULL for a match of the component name
 * @param match Where the text starts in the value
 * @param len Length of the text
 */
static void box_search_hit(
    box_search_out_t *out,
    const char *path,
    const char *id,
    const char *value,
    const char *match,
    int32_t len)
{
    const char *start, *end;
    out->count ++;
    if (out->count > BOX_SEARCH_LIMIT) {
        goto done;
    }

    if (ecs_strbuf_written(&out->buf)) {
        ecs_strbuf_appendch(&out->buf, '\n');
    }

    ecs_strbuf_appendstr(&out->buf, path);
    if (id) {
        ecs_strbuf_append(&out->buf, ":%s", id);
    }

    if (!value) {
        goto done;
    }

    start = match - value > BOX_SEARCH_CONTEXT ? match - BOX_SEARCH_CONTEXT : value;
    end = match + len;
    end = ecs_os_strlen(end) > BOX_SEARCH_CONTEXT ? end + BOX_SEARCH_CONTEXT : end + strlen(end);
    ecs_strbuf_append(&out->buf, ": %s%.*s%s", start == value ? "" : "...",
        (int)(end - start), start, end[0] ? "..." : "");
done:
    return;
}

/** Test whether grep skips a component because it holds the name or the parent.
 *
 * @param id The component
 * @return Whether the component is skipped
 */
static bool box_search_hidden(
    ecs_id_t id)
{
    bool result = id == ecs_id(EcsParent);
    if (ECS_IS_PAIR(id)) {
        result = ECS_PAIR_FIRST(id) == ecs_id(EcsIdentifier) || ECS_PAIR_FIRST(id) == EcsChildOf;
    }

    return result;
}

/** Search the name, the component names and the component values of an entity.
 *
 * @param world The world
 * @param entry The entity
 * @param filter The only component to search the value of, 0 for all
 * @param text The text
 * @param out The output
 */
static void box_search_scan(
    ecs_world_t *world,
    const box_search_entity_t *entry,
    ecs_id_t filter,
    const char *text,
    box_search_out_t *out)
{
    const char *name = ecs_get_name(world, entry->e);
    const ecs_type_t *type = ecs_get_type(world, entry->e);
    int32_t i, len = ecs_os_strlen(text);
    if (!filter && name && strstr(name, text)) {
        box_search_hit(out, entry->path, NULL, NULL, NULL, 0);
    }

    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        ecs_entity_t vtype;
        const void *ptr;
        char *str, *value = NULL;
        const char *match = NULL;
        if (filter ? id != filter : box_search_hidden(id)) {
            continue;
        }

        vtype = ecs_get_typeid(world, id);
        ptr = vtype && ecs_has(world, vtype, EcsTypeSerializer) ?
            ecs_get_id(world, entry->e, id) : NULL;
        value = ptr ? ecs_ptr_to_expr(world, vtype, ptr) : NULL;
        match = value ? strstr(value, text) : NULL;
        str = ecs_id_str(world, id);
        if (match) {
            box_search_hit(out, entry->path, str, value, match, len);
        } else if (!filter && strstr(str, text)) {
            box_search_hit(out, entry->path, str, NULL, NULL, 0);
        }

        ecs_os_free(str);
        ecs_os_free(value);
    }
}

/** Search the entities in the current scope of a session.
 *
 * @param ctx The call context
 * @param argv The session, the component or text, and the text
 * @param result Receives the hits, one per line
 * @param recur Search all descendants instead of the children
 */
static void box_search_grep_run(
    const ecs_function_ctx_t *ctx,
    const ecs_value_t *argv,
    ecs_value_t *result,
    bool recur)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *component = *(char**)argv[1].ptr;
    const char *text = *(char**)argv[2].ptr;
    ecs_entity_t scope = box_terminal_cwd(world, session);
    box_search_out_t out = { .buf = ECS_STRBUF_INIT };
    box_search_entity_t *entries;
    ecs_id_t filter = 0;
    ecs_vec_t vec;
    int32_t i;
    int prev;
    ecs_vec_init_t(NULL, &vec, box_search_entity_t, 0);
    if (!text) {
        text = component;
        component = NULL;
    }

    if (component) {
        prev = ecs_log_set_level(-4);
        filter = box_command_component(world, component);
        ecs_log_set_level(prev);
        if (!filter) {
            box_command_error(ctx, result, "unknown component '%s'", component);
            goto done;
        }
    }

    if (scope) {
        box_search_add_children(world, scope, &vec);
    } else {
        box_search_add_root(world, &vec);
    }

    for (i = 0; recur && i < ecs_vec_count(&vec); i ++) {
        box_search_add_children(world, ecs_vec_get_t(&vec, box_search_entity_t, i)->e, &vec);
    }

    entries = ecs_vec_first(&vec);
    if (entries) {
        qsort(entries, (size_t)ecs_vec_count(&vec), sizeof(box_search_entity_t),
            box_search_compare);
    }

    for (i = 0; i < ecs_vec_count(&vec); i ++) {
        box_search_scan(world, &entries[i], filter, text, &out);
    }

    if (out.count > BOX_SEARCH_LIMIT) {
        ecs_strbuf_append(&out.buf, "\n... %d more", out.count - BOX_SEARCH_LIMIT);
    }

    *(char**)result->ptr = ecs_strbuf_get(&out.buf);
done:
    entries = ecs_vec_first(&vec);
    for (i = 0; i < ecs_vec_count(&vec); i ++) {
        ecs_os_free(entries[i].path);
    }

    ecs_vec_fini_t(NULL, &vec, box_search_entity_t);
    ecs_strbuf_reset(&out.buf);
}

/** The grep command: search the children of the current scope.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the component or text, and the text
 * @param result Receives the hits
 */
static void box_search_grep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_search_grep_run(ctx, argv, result, false);
}

/** The grep_recur command: search all descendants of the current scope.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the component or text, and the text
 * @param result Receives the hits
 */
static void box_search_grep_recur(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    box_search_grep_run(ctx, argv, result, true);
}

void box_search_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, SearchIter);
    ecs_set_hooks(world, SearchIter, { .ctor = flecs_default_ctor });
    ecs_add_pair(world, ecs_id(SearchIter), EcsOnInstantiate, EcsDontInherit);

    box_command_register(world, &(box_command_desc_t){
        .name = "query",
        .brief = "Create a named query; lists queries without arguments",
        .params = {
            { .name = "name", .type = ecs_id(ecs_string_t), .optional = true },
            { .name = "expr", .type = ecs_id(ecs_string_t), .optional = true, .rest = true }
        },
        .callback = box_search_query
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "iter",
        .brief = "Iterate a named query with next; any other command ends iteration",
        .params = {{ .name = "name", .type = ecs_id(ecs_string_t) }},
        .callback = box_search_iter
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "next",
        .brief = "Print the next count results of the iterated query, one when omitted",
        .params = {{ .name = "count", .type = ecs_id(ecs_i32_t), .optional = true }},
        .callback = box_search_next
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "done",
        .brief = "Stop iterating",
        .callback = box_search_done
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "has",
        .brief = "Test whether an entity has a component or matches a named query or expression",
        .params = {
            { .name = "entity", .type = ecs_id(ecs_entity_t) },
            { .name = "query", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = box_search_has
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "grep",
        .brief = "Search names, components and values of the children of the current path",
        .params = {
            { .name = "component", .type = ecs_id(ecs_string_t) },
            { .name = "text", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = box_search_grep
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "grep_recur",
        .brief = "Search names, components and values of everything under the current path",
        .params = {
            { .name = "component", .type = ecs_id(ecs_string_t) },
            { .name = "text", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = box_search_grep_recur
    });
}
