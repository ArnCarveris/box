/**
 * @file command/search.c
 * @brief The query, iter, next, done, grep and grep_recur commands.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** Lines printed by next and grep before the output is capped */
#define BOX_SEARCH_LIMIT (100)

/** Levels of nested members grep descends into before it prints a value whole */
#define BOX_SEARCH_DEPTH (16)

/** Iteration state of a session, kept on the heap so the iterator can resume */
typedef struct box_search_iter_t {
    const ecs_query_t *query;  /**< Query being iterated, NULL when idle */
    ecs_entity_t entity;       /**< Entity that holds the query */
    ecs_iter_t it;             /**< The open iterator */
    int32_t row;               /**< Next row of the current result */
    bool open;                 /**< The iterator must still be finished */
    bool result;               /**< The iterator holds a result with rows */
} box_search_iter_t;

/** Private search state of a terminal session */
typedef struct SearchState {
    box_search_iter_t *iter;  /**< Iteration state, freed with the session */
} SearchState;

/** An entity collected by grep before its components are serialized */
typedef struct box_search_entity_t {
    char *path;      /**< Path of the entity */
    ecs_entity_t e;  /**< The entity */
} box_search_entity_t;

/** Output of a grep command, capped at BOX_SEARCH_LIMIT lines */
typedef struct box_search_out_t {
    ecs_strbuf_t buf;  /**< The printed lines */
    int32_t count;     /**< Hits found, including the ones over the limit */
} box_search_out_t;

/** The component value a grep walks member by member */
typedef struct box_search_walk_t {
    ecs_world_t *world;      /**< The world */
    const char *path;        /**< Path of the entity */
    const char *id;          /**< Name of the component */
    const char *text;        /**< The text to search for */
    box_search_out_t *out;   /**< The output */
    int32_t hits;            /**< Lines the value added to the output */
} box_search_walk_t;

ECS_COMPONENT_DECLARE(SearchState);

/** Finish the iterator of a session and leave iteration mode.
 *
 * @param s The iteration state
 */
static void box_search_close(
    box_search_iter_t *s)
{
    if (s->open) {
        ecs_iter_fini(&s->it);
    }

    s->open = false;
    s->result = false;
    s->row = 0;
    s->query = NULL;
    s->entity = 0;
}

/** SearchState destructor: finish the iterator and free the state.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_search_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    SearchState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        if (state[i].iter) {
            box_search_close(state[i].iter);
            ecs_os_free(state[i].iter);
            state[i].iter = NULL;
        }
    }
}

/** A query entity is deleted: finish the iterator that reads it.
 *
 * @param it Iterator over the deleted query entities
 */
static void box_search_on_query_remove(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_entity_t parent = box_hierarchy_parent(world, e);
        if (!parent || !ecs_is_alive(world, parent)) {
            continue;
        }

        SearchState *state = ecs_get_mut(world, parent, SearchState);
        if (state && state->iter && state->iter->entity == e) {
            box_search_close(state->iter);
        }
    }
}

/** Get the iteration state of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param create Add the state when the session has none
 * @return The iteration state, NULL when the session has none
 */
static box_search_iter_t* box_search_state(
    ecs_world_t *world,
    ecs_entity_t session,
    bool create)
{
    box_search_iter_t *result = NULL;
    SearchState *state = ecs_get_mut(world, session, SearchState);
    if (state) {
        result = state->iter;
        goto done;
    }

    if (!create) {
        goto done;
    }

    result = ecs_os_calloc_t(box_search_iter_t);
    ecs_set(world, session, SearchState, { .iter = result });

done:
    return result;
}

/** Test whether a command line starts with a command name.
 *
 * @param line The command line
 * @param name The command name
 * @return true when the line runs the command
 */
static bool box_search_is_cmd(
    const char *line,
    const char *name)
{
    while (line[0] == ' ' || line[0] == '\t') {
        line ++;
    }

    size_t len = strlen(name);
    bool result = !strncmp(line, name, len);
    if (result && line[len] && line[len] != ' ' && line[len] != '\t') {
        result = false;
    }

    return result;
}

/** Test whether the previous command line of a session continued an iteration.
 *
 * Any command other than iter and next leaves iteration mode, which is what
 * the last logged line of the session tells.
 *
 * @param world The world
 * @param session The session entity
 * @return true when the previous line was an iter or next command
 */
static bool box_search_resumed(
    ecs_world_t *world,
    ecs_entity_t session)
{
    bool result = false;
    const TerminalSession *s = ecs_get(world, session, TerminalSession);
    if (!s || !s->count) {
        goto done;
    }

    const char *line =
        s->ring[box_ring_slot(s->count - 1, BOX_COMMAND_LOG_MAX)].line;
    if (!line) {
        goto done;
    }

    result = box_search_is_cmd(line, "iter") || box_search_is_cmd(line, "next");

done:
    return result;
}

/** Serialize the value a field of an iterator holds for one row.
 *
 * @param it The iterator
 * @param field The field index
 * @param row The row in the result
 * @return The value, NULL when the field has no data, the caller frees it
 */
static char* box_search_value(
    const ecs_iter_t *it,
    int8_t field,
    int32_t row)
{
    char *result = NULL;
    if (!ecs_field_is_set(it, field)) {
        goto done;
    }

    size_t size = ecs_field_size(it, field);
    ecs_entity_t type = ecs_get_typeid(it->world, ecs_field_id(it, field));
    if (!size || !type) {
        goto done;
    }

    void *ptr;
    if (it->row_fields & (ecs_termset_t)(1u << field)) {
        ptr = ecs_field_at_w_size(it, size, field, row);
    } else {
        ptr = ecs_field_w_size(it, size, field);
        if (ptr && ecs_field_is_self(it, field)) {
            ptr = ECS_ELEM(ptr, (ecs_size_t)size, row);
        }
    }

    if (ptr && ecs_has(it->world, type, EcsTypeSerializer)) {
        result = ecs_ptr_to_expr(it->world, type, ptr);
    }

done:
    return result;
}

/** Append one matched entity as its path and the values of its terms.
 *
 * @param it The iterator
 * @param row The row in the result
 * @param buf The buffer
 */
static void box_search_append_row(
    const ecs_iter_t *it,
    int32_t row,
    ecs_strbuf_t *buf)
{
    char *path = box_path_str(it->world, it->entities[row]);
    ecs_strbuf_appendstr(buf, path);
    ecs_os_free(path);

    int8_t i;
    for (i = 0; i < it->field_count; i ++) {
        char *value = box_search_value(it, i, row);
        if (value) {
            ecs_strbuf_append(buf, "\t%s", value);
            ecs_os_free(value);
        }
    }

    ecs_strbuf_appendstr(buf, "\n");
}

/** Append the entities the iterator of a session has left.
 *
 * @param s The iteration state
 * @param limit Number of entities to print
 * @param buf The buffer
 * @return Number of entities printed, less than limit when the query ran out
 */
static int32_t box_search_advance(
    box_search_iter_t *s,
    int32_t limit,
    ecs_strbuf_t *buf)
{
    int32_t result = 0;
    while (result < limit) {
        if (!s->result || s->row >= s->it.count) {
            if (!ecs_query_next(&s->it)) {
                s->open = false;
                s->result = false;
                break;
            }

            s->result = s->it.entities != NULL;
            s->row = 0;
            continue;
        }

        box_search_append_row(&s->it, s->row, buf);
        s->row ++;
        result ++;
    }

    return result;
}

/** Append a grep hit, counting the hits that do not fit in the output.
 *
 * A hit prints as path, path:id, path:id: value or path:id:line: text.
 *
 * @param out The output
 * @param path Path of the entity
 * @param id Component and member, NULL for a hit on the entity name
 * @param line Line of a multi-line string, 0 for a whole value
 * @param value The value or the line of a string, NULL when it has none
 */
static void box_search_append_hit(
    box_search_out_t *out,
    const char *path,
    const char *id,
    int32_t line,
    const char *value)
{
    if (out->count < BOX_SEARCH_LIMIT) {
        ecs_strbuf_appendstr(&out->buf, path);
        if (id) {
            ecs_strbuf_append(&out->buf, ":%s", id);
        }

        if (line) {
            ecs_strbuf_append(&out->buf, ":%d", line);
        }

        if (value) {
            ecs_strbuf_append(&out->buf, ": %s", value);
        }

        ecs_strbuf_appendstr(&out->buf, "\n");
    }

    out->count ++;
}

/** Name a member of a component the way a grep hit prints it.
 *
 * @param w The walk
 * @param member Path of the member inside the component, empty for the value itself
 * @return Component.member, the caller frees it
 */
static char* box_search_member_id(
    const box_search_walk_t *w,
    const char *member)
{
    char *result;
    if (member[0]) {
        result = flecs_asprintf("%s.%s", w->id, member);
    } else {
        result = ecs_os_strdup(w->id);
    }

    return result;
}

/** Print the lines of a multi-line string member that contain the text.
 *
 * @param w The walk
 * @param member Path of the member inside the component
 * @param str The string
 */
static void box_search_lines(
    box_search_walk_t *w,
    const char *member,
    const char *str)
{
    char *id = box_search_member_id(w, member);
    const char *p = str;
    int32_t line = 1;
    while (p) {
        const char *end = strchr(p, '\n');
        int32_t len = end ? (int32_t)(end - p) : (int32_t)strlen(p);
        const char *found = strstr(p, w->text);
        if (found && (int32_t)(found - p) < len) {
            char *text = ecs_os_malloc(len + 1);
            ecs_os_memcpy(text, p, len);
            text[len] = '\0';
            box_search_append_hit(w->out, w->path, id, line, text);
            ecs_os_free(text);
            w->hits ++;
        }

        p = end ? end + 1 : NULL;
        line ++;
    }

    ecs_os_free(id);
}

/** Print a value that grep does not split further when it contains the text.
 *
 * The text is matched against `member: value`, or the value alone for the
 * value of the component itself.
 *
 * @param w The walk
 * @param type The type of the value
 * @param ptr The value
 * @param member Path of the member inside the component, empty for the value itself
 */
static void box_search_leaf(
    box_search_walk_t *w,
    ecs_entity_t type,
    const void *ptr,
    const char *member)
{
    char *value = NULL;
    if (ecs_has(w->world, type, EcsTypeSerializer)) {
        value = ecs_ptr_to_expr(w->world, type, ptr);
    }

    if (!value) {
        goto done;
    }

    char *line = member[0] ? flecs_asprintf("%s: %s", member, value) : NULL;
    if (strstr(line ? line : value, w->text)) {
        char *id = box_search_member_id(w, member);
        box_search_append_hit(w->out, w->path, id, 0, value);
        ecs_os_free(id);
        w->hits ++;
    }

    ecs_os_free(line);

done:
    ecs_os_free(value);
}

/** Walk a value member by member and print the ones that contain the text.
 *
 * Structs, arrays and vectors print one line per member or element, strings
 * with line breaks one line per matching line and other values print whole.
 *
 * @param w The walk
 * @param type The type of the value
 * @param ptr The value
 * @param member Path of the value inside the component, empty for the component
 * @param count Elements of an inline array member, 0 for a single value
 * @param depth Nesting level of the value
 */
static void box_search_walk(
    box_search_walk_t *w,
    ecs_entity_t type,
    const void *ptr,
    const char *member,
    int32_t count,
    int32_t depth)
{
    const EcsType *meta = ecs_get(w->world, type, EcsType);
    ecs_type_kind_t kind = meta && depth < BOX_SEARCH_DEPTH ?
        meta->kind : EcsOpaqueType;
    ecs_entity_t elem_type = count ? type : 0;
    const void *first = ptr;
    if (!count && kind == EcsArrayType) {
        const EcsArray *a = ecs_get(w->world, type, EcsArray);
        elem_type = a->type;
        count = a->count;
    } else if (!count && kind == EcsVectorType) {
        const EcsVector *v = ecs_get(w->world, type, EcsVector);
        const ecs_vec_t *vec = ptr;
        elem_type = v->type;
        first = vec->array;
        count = vec->count;
    }

    if (elem_type) {
        const EcsComponent *component = ecs_get(w->world, elem_type, EcsComponent);
        ecs_size_t size = component ? component->size : 0;
        int32_t i;
        for (i = 0; i < count && size && first; i ++) {
            char *name = flecs_asprintf("%s[%d]", member, i);
            box_search_walk(w, elem_type, ECS_ELEM(first, size, i), name, 0, depth + 1);
            ecs_os_free(name);
        }
    } else if (kind == EcsStructType) {
        const EcsStruct *st = ecs_get(w->world, type, EcsStruct);
        int32_t i, members = st ? ecs_vec_count(&st->members) : 0;
        for (i = 0; i < members; i ++) {
            const ecs_member_t *m = ecs_vec_get_t(&st->members, ecs_member_t, i);
            char *name = member[0] ?
                flecs_asprintf("%s.%s", member, m->name) : ecs_os_strdup(m->name);
            box_search_walk(w, m->type, ECS_OFFSET(ptr, m->offset), name,
                m->count > 1 ? m->count : 0, depth + 1);
            ecs_os_free(name);
        }
    } else {
        const EcsPrimitive *prim = kind == EcsPrimitiveType ?
            ecs_get(w->world, type, EcsPrimitive) : NULL;
        const char *str = prim && prim->kind == EcsString ?
            *(const char* const*)ptr : NULL;
        if (str && strchr(str, '\n')) {
            box_search_lines(w, member, str);
        } else {
            box_search_leaf(w, type, ptr, member);
        }
    }
}

/** Test whether a component of an entity is hidden from grep.
 *
 * @param id The component id
 * @return true when the component holds the name or the parent
 */
static bool box_search_hidden(
    ecs_id_t id)
{
    bool result = false;
    if (ECS_IS_PAIR(id)) {
        ecs_entity_t first = ECS_PAIR_FIRST(id);
        result = first == ecs_id(EcsIdentifier) || first == EcsChildOf;
    }

    return result;
}

/** Match the name, component names and component members of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param path Path of the entity
 * @param filter The component to search, 0 for every component
 * @param text The text to search for
 * @param out The output
 */
static void box_search_scan(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *path,
    ecs_id_t filter,
    const char *text,
    box_search_out_t *out)
{
    const char *name = ecs_get_name(world, e);
    if (!filter && name && strstr(name, text)) {
        box_search_append_hit(out, path, NULL, 0, NULL);
    }

    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i, count = type ? type->count : 0;
    for (i = 0; i < count; i ++) {
        ecs_id_t id = type->array[i];
        if (filter ? id != filter : box_search_hidden(id)) {
            continue;
        }

        char *id_str = ecs_id_str(world, id);
        ecs_entity_t vtype = ecs_get_typeid(world, id);
        const void *ptr = vtype ? ecs_get_id(world, e, id) : NULL;
        box_search_walk_t w = {
            .world = world, .path = path, .id = id_str, .text = text, .out = out
        };

        if (ptr) {
            box_search_walk(&w, vtype, ptr, "", 0, 0);
        }

        if (!w.hits && (!filter || !ptr) && strstr(id_str, text)) {
            box_search_append_hit(out, path, id_str, 0, NULL);
        }

        ecs_os_free(id_str);
    }
}

/** Compare two collected entities by path.
 *
 * @param a The first entity
 * @param b The second entity
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_search_compare(
    const void *a,
    const void *b)
{
    const box_search_entity_t *e1 = a;
    const box_search_entity_t *e2 = b;

    return strcmp(e1->path, e2->path);
}

/** Append an entity with its path to a vector.
 *
 * @param world The world
 * @param e The entity
 * @param out The vector
 */
static void box_search_append_entity(
    ecs_world_t *world,
    ecs_entity_t e,
    void *out)
{
    box_search_entity_t *entry =
        ecs_vec_append_t(NULL, out, box_search_entity_t);
    entry->e = e;
    entry->path = box_path_str(world, e);
}

/** Append the named entities that have no parent to a vector.
 *
 * @param world The world
 * @param out The vector
 */
static void box_search_collect_root(
    ecs_world_t *world,
    ecs_vec_t *out)
{
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_pair_t(EcsIdentifier, EcsName) },
            { .id = ecs_pair(EcsChildOf, EcsWildcard), .oper = EcsNot }
        }
    });

    ecs_assert(q != NULL, ECS_INVALID_OPERATION, "failed to create root query");

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_search_append_entity(world, it.entities[i], out);
        }
    }

    ecs_query_fini(q);
}

/** Append the children of a scope to a vector.
 *
 * @param world The world
 * @param scope The scope, 0 for the root; a wildcard has no children
 * @param out The vector
 */
static void box_search_collect_children(
    ecs_world_t *world,
    ecs_entity_t scope,
    ecs_vec_t *out)
{
    if (!scope) {
        box_search_collect_root(world, out);
    } else if (!ecs_id_is_wildcard(scope)) {
        box_hierarchy_children(world, scope, box_search_append_entity, out);
    }
}

/** Collect the entities that a grep searches, sorted by path.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @param recur Collect every descendant instead of the children
 * @param out The vector
 */
static void box_search_collect(
    ecs_world_t *world,
    ecs_entity_t scope,
    bool recur,
    ecs_vec_t *out)
{
    box_search_collect_children(world, scope, out);

    if (recur) {
        int32_t i = 0;
        while (i < ecs_vec_count(out)) {
            ecs_entity_t e =
                ecs_vec_get_t(out, box_search_entity_t, i)->e;
            box_search_collect_children(world, e, out);
            i ++;
        }
    }

    qsort(ecs_vec_first(out), (size_t)ecs_vec_count(out),
        sizeof(box_search_entity_t), box_search_compare);
}

/** Search the entities under the current path of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param component The component to search, NULL for every component
 * @param text The text to search for
 * @param recur Search every descendant instead of the children
 * @return The output, the caller frees it
 */
static char* box_search_grep_run(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *component,
    const char *text,
    bool recur)
{
    char *result = NULL;
    ecs_id_t filter = 0;
    if (component) {
        filter = box_path_id(world, component);
        if (!filter) {
            result = flecs_asprintf(
                BOX_COMMAND_ERROR_PREFIX "unknown component '%s'", component);
            goto done;
        }
    }

    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, box_search_entity_t, 0);
    box_search_collect(
        world, box_path_cwd(world, session), recur, &entities);

    box_search_out_t out = { .buf = ECS_STRBUF_INIT };
    int32_t i, count = ecs_vec_count(&entities);
    for (i = 0; i < count; i ++) {
        box_search_entity_t *entry =
            ecs_vec_get_t(&entities, box_search_entity_t, i);
        box_search_scan(world, entry->e, entry->path, filter, text, &out);
        ecs_os_free(entry->path);
    }

    ecs_vec_fini_t(NULL, &entities, box_search_entity_t);

    if (out.count > BOX_SEARCH_LIMIT) {
        ecs_strbuf_append(&out.buf, "... %d more\n",
            out.count - BOX_SEARCH_LIMIT);
    }

    result = ecs_strbuf_get(&out.buf);

done:
    return result;
}

/** Run a grep with the arguments of a grep command.
 *
 * @param world The world
 * @param session The session entity
 * @param first The first argument, a component when a second one is given
 * @param second The second argument, NULL when it was omitted
 * @param recur Search every descendant instead of the children
 * @return The output, the caller frees it
 */
static char* box_search_grep_args(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *first,
    const char *second,
    bool recur)
{
    char *result;
    if (!first) {
        result = ecs_os_strdup(
            BOX_COMMAND_ERROR_PREFIX "no text to search for");
    } else {
        result = box_search_grep_run(
            world, session, second ? first : NULL,
            second ? second : first, recur);
    }

    return result;
}

/** The query command: create or replace a named query under the session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
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
    char *out = NULL;
    box_search_iter_t *s = box_search_state(world, session, false);
    if (s) {
        box_search_close(s);
    }

    ecs_entity_t existing = ecs_lookup_child(world, session, name);
    if (existing && !ecs_query_get(world, existing)) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "'%s' is not a query", name);
        goto done;
    }

    if (existing) {
        ecs_delete(world, existing);
    }

    ecs_entity_t e = ecs_entity(world, {
        .name = name, .parent = session, .sep = ""
    });

    int prev = ecs_log_set_level(-4);
    ecs_query_t *q = ecs_query(world, { .expr = expr, .entity = e });
    ecs_log_set_level(prev);
    if (!q) {
        ecs_delete(world, e);
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid query '%s'", expr);
        goto done;
    }

    out = box_path_str(world, e);

done:
    *(char**)result->ptr = out;
}

/** The iter command: put the session in iteration mode over a named query.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
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
    char *out = NULL;
    ecs_entity_t e = ecs_lookup_child(world, session, name);
    const ecs_query_t *q = e ? ecs_query_get(world, e) : NULL;
    if (!q) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "unknown query '%s'", name);
        goto done;
    }

    box_search_iter_t *s = box_search_state(world, session, true);
    box_search_close(s);
    s->query = q;
    s->entity = e;
    s->it = ecs_query_iter(world, q);
    s->open = true;
    out = box_path_str(world, e);

done:
    *(char**)result->ptr = out;
}

/** The next command: print the next matched entities of the iterator.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
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
    char *out = NULL;
    box_search_iter_t *s = box_search_state(world, session, false);
    bool active = s && s->open && box_search_resumed(world, session);
    if (!active) {
        if (s) {
            box_search_close(s);
        }

        out = ecs_os_strdup(BOX_COMMAND_ERROR_PREFIX "no active iterator");
        goto done;
    }

    if (count <= 0) {
        count = 1;
    }

    int32_t limit = count < BOX_SEARCH_LIMIT ? count : BOX_SEARCH_LIMIT;
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t printed = box_search_advance(s, limit, &buf);
    if (!s->open) {
        ecs_strbuf_appendstr(&buf, "(no more)\n");
    } else if (count > printed) {
        ecs_strbuf_append(&buf, "... %d more\n", count - printed);
    }

    out = ecs_strbuf_get(&buf);

done:
    *(char**)result->ptr = out;
}

/** The done command: finish the iterator of the session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_search_done(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    char *out = NULL;
    box_search_iter_t *s = box_search_state(world, session, false);
    if (s && s->entity) {
        out = box_path_str(world, s->entity);
    }

    if (s) {
        box_search_close(s);
    }

    if (!out) {
        out = ecs_os_strdup("");
    }

    *(char**)result->ptr = out;
}

/** The grep command: search the children of the current path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_search_grep(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *first = *(char**)argv[1].ptr;
    const char *second = *(char**)argv[2].ptr;

    *(char**)result->ptr = box_search_grep_args(
        ctx->world, session, first, second, false);
}

/** The grep_recur command: search every entity under the current path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_search_grep_recur(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *first = *(char**)argv[1].ptr;
    const char *second = *(char**)argv[2].ptr;

    *(char**)result->ptr = box_search_grep_args(
        ctx->world, session, first, second, true);
}

void box_search_init(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, SearchState);

    ecs_set_hooks(world, SearchState, {
        .ctor = flecs_default_ctor,
        .dtor = box_search_state_dtor
    });
    ecs_add_pair(world, ecs_id(SearchState), EcsOnInstantiate, EcsDontInherit);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(ecs_id(EcsPoly), EcsQuery) }},
        .events = { EcsOnRemove },
        .callback = box_search_on_query_remove
    });

    box_command_param_t query[] = {
        { .name = "name", .type = ecs_id(ecs_string_t) },
        { .name = "expr", .type = ecs_id(ecs_string_t) }
    };

    box_command_param_t name[] = {{
        .name = "name", .type = ecs_id(ecs_string_t)
    }};

    box_command_param_t count[] = {{
        .name = "count", .type = ecs_id(ecs_i32_t), .optional = true
    }};

    box_command_param_t grep[] = {
        { .name = "component", .type = ecs_id(ecs_string_t), .optional = true },
        { .name = "text", .type = ecs_id(ecs_string_t), .optional = true }
    };

    box_command_register(world, "query",
        "Create a named query under the session", query, 2, box_search_query);

    box_command_register(world, "iter",
        "Start iterating a named query", name, 1, box_search_iter);

    box_command_register(world, "next",
        "Print the next matched entities of the iterator", count, 1,
        box_search_next);

    box_command_register(world, "done",
        "Finish the iterator of the session", NULL, 0, box_search_done);

    box_command_register(world, "grep",
        "Search the children of the current path for a text", grep, 2,
        box_search_grep);

    box_command_register(world, "grep_recur",
        "Search every entity under the current path for a text", grep, 2,
        box_search_grep_recur);
}
