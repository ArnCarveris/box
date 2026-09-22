/**
 * @file command/shell.c
 * @brief The cd, ls, touch, rm, mv, add, remove, set, get, clear, has and man commands.
 */

#include "command_internal.h"
#include <string.h>
#include <stdlib.h>

/** A child entity listed by ls */
typedef struct box_shell_child_t {
    char *name;      /**< Name of the child, #id when it has none */
    ecs_entity_t e;  /**< The child entity */
} box_shell_child_t;

/** Compare two listed children by name.
 *
 * @param a The first child
 * @param b The second child
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_shell_compare(
    const void *a,
    const void *b)
{
    const box_shell_child_t *c1 = a;
    const box_shell_child_t *c2 = b;

    return strcmp(c1->name, c2->name);
}

/** Resolve a path argument against the current path of a session.
 *
 * @param world The world
 * @param session The session entity
 * @param path The path argument, NULL for the current path
 * @param out Receives the entity, 0 for the root
 * @return NULL on success, a one line error the caller frees
 */
static char* box_shell_resolve(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    ecs_entity_t *out)
{
    char *result = NULL;
    ecs_entity_t cwd = box_path_cwd(world, session);
    if (!path) {
        out[0] = cwd;
        goto done;
    }

    if (box_path_lookup(world, cwd, path, out)) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "unknown entity '%s'", path);
    }

done:
    return result;
}

/** Resolve a path argument that must name an entity other than the session.
 *
 * @param world The world
 * @param session The session entity
 * @param path The path argument
 * @param out Receives the entity
 * @return NULL on success, a one line error the caller frees
 */
static char* box_shell_target(
    ecs_world_t *world,
    ecs_entity_t session,
    const char *path,
    ecs_entity_t *out)
{
    char *result = box_shell_resolve(world, session, path, out);
    if (!result && !out[0]) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR_PREFIX "the root has no components");
    }

    if (!result && out[0] == session) {
        result = ecs_os_strdup(BOX_COMMAND_ERROR_PREFIX "cannot change the session");
    }

    return result;
}

/** Test whether an expression holds one component id and not a query.
 *
 * @param expr The expression
 * @return true when the expression is a single component or pair
 */
static bool box_shell_is_id(
    const char *expr)
{
    bool result = true;
    int32_t depth = 0;
    const char *p;
    for (p = expr; p[0]; p ++) {
        if (p[0] == '(') {
            depth ++;
        } else if (p[0] == ')') {
            depth --;
        } else if (!depth && strchr(",!|?$ \t", p[0])) {
            result = false;
        }
    }

    return result;
}

/** Resolve a component or pair argument written as Comp or (Rel,Tgt).
 *
 * @param world The world
 * @param expr The component expression
 * @param out Receives the id
 * @return NULL on success, a one line error the caller frees
 */
static char* box_shell_id(
    ecs_world_t *world,
    const char *expr,
    ecs_id_t *out)
{
    out[0] = 0;
    if (box_shell_is_id(expr)) {
        out[0] = box_path_id(world, expr);
    }

    char *result = NULL;
    if (!out[0]) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "unknown component '%s'", expr);
    }

    return result;
}

/** Append a listed child to a vector of children.
 *
 * @param world The world
 * @param e The child entity
 * @param children The vector
 */
static void box_shell_add_child(
    ecs_world_t *world,
    ecs_entity_t e,
    void *children)
{
    box_shell_child_t *child = ecs_vec_append_t(NULL, children, box_shell_child_t);
    const char *name = ecs_get_name(world, e);
    child->e = e;
    child->name = name ?
        box_path_escape(name) : flecs_asprintf("#%u", (uint32_t)e);
}

/** Collect the named entities that have no parent.
 *
 * @param world The world
 * @param children The vector that receives the entities
 */
static void box_shell_collect_root(
    ecs_world_t *world,
    ecs_vec_t *children)
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
            box_shell_add_child(world, it.entities[i], children);
        }
    }

    ecs_query_fini(q);
}

/** Collect the children of a scope, sorted by name.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @param children The vector that receives the children
 */
static void box_shell_collect(
    ecs_world_t *world,
    ecs_entity_t scope,
    ecs_vec_t *children)
{
    if (!scope) {
        box_shell_collect_root(world, children);
    } else {
        box_hierarchy_children(world, scope, box_shell_add_child, children);
    }

    qsort(ecs_vec_first(children), (size_t)ecs_vec_count(children),
        sizeof(box_shell_child_t), box_shell_compare);
}

/** Render every component of an entity with its value.
 *
 * @param world The world
 * @param e The entity
 * @return One id or `id: value` line per component, the caller frees it
 */
static char* box_shell_values(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i, count = type ? type->count : 0;
    for (i = 0; i < count; i ++) {
        ecs_id_t id = type->array[i];
        if (ECS_IS_PAIR(id) && ECS_PAIR_FIRST(id) == ecs_id(EcsIdentifier)) {
            continue;
        }

        char *name = ecs_id_str(world, id);
        ecs_entity_t vtype = ecs_get_typeid(world, id);
        const void *ptr = vtype ? ecs_get_id(world, e, id) : NULL;
        char *value = ptr ? ecs_ptr_to_expr(world, vtype, ptr) : NULL;
        if (value) {
            ecs_strbuf_append(&buf, "%s: %s\n", name, value);
        } else {
            ecs_strbuf_append(&buf, "%s\n", name);
        }

        ecs_os_free(value);
        ecs_os_free(name);
    }

    return ecs_strbuf_get(&buf);
}

/** Test an entity against a query expression.
 *
 * @param world The world
 * @param e The entity
 * @param expr The query expression
 * @param out Receives true when the entity matches
 * @return NULL on success, a one line error the caller frees
 */
static char* box_shell_match(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *expr,
    bool *out)
{
    int prev = ecs_log_set_level(-4);
    ecs_query_t *q = ecs_query(world, { .expr = expr });
    ecs_log_set_level(prev);

    char *result = NULL;
    if (!q) {
        result = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "invalid query '%s'", expr);
        goto done;
    }

    ecs_iter_t it;
    out[0] = ecs_query_has(q, e, &it);
    if (out[0]) {
        ecs_iter_fini(&it);
    }

    ecs_query_fini(q);

done:
    return result;
}

/** The cd command: change the current path of the session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_cd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_resolve(world, session, path ? path : "", &e);
    if (out) {
        goto done;
    }

    out = box_path_str(world, e);

    Terminal *t = ecs_get_mut(world, session, Terminal);
    ecs_assert(t != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");
    ecs_os_free(t->cwd);
    t->cwd = e ? ecs_os_strdup(out) : NULL;
    ecs_modified(world, session, Terminal);

done:
    *(char**)result->ptr = out;
}

/** The pwd command: print the current path of the session.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_pwd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    *(char**)result->ptr = box_path_str(world, box_path_cwd(world, session));
}

/** The ls command: list the children of the current path or of a path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_ls(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t scope = 0;
    char *out = box_shell_resolve(world, session, path, &scope);
    if (out) {
        goto done;
    }

    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, box_shell_child_t, 0);
    box_shell_collect(world, scope, &children);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, count = ecs_vec_count(&children);
    for (i = 0; i < count; i ++) {
        box_shell_child_t *child = ecs_vec_get_t(&children, box_shell_child_t, i);
        ecs_strbuf_appendstr(&buf, child->name);
        ecs_strbuf_appendstr(&buf, "\n");
        ecs_os_free(child->name);
    }

    ecs_vec_fini_t(NULL, &children, box_shell_child_t);
    out = ecs_strbuf_get(&buf);

done:
    *(char**)result->ptr = out;
}

/** The touch command: create the entity of a path and its missing parents.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_touch(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t cwd = box_path_cwd(world, session);
    ecs_entity_t e = box_path_ensure(world, cwd, path);
    char *out;
    if (!e) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' is not a valid path", path);
    } else {
        out = box_path_str(world, e);
    }

    *(char**)result->ptr = out;
}

/** The rm command: delete the entity of a path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_rm(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    out = box_path_str(world, e);
    ecs_delete(world, e);

done:
    *(char**)result->ptr = out;
}

/** The mv command: rename or reparent an entity to another path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_mv(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *from = *(char**)argv[1].ptr;
    const char *to = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    char *name = NULL;
    char *out = box_shell_target(world, session, from, &e);
    if (out) {
        goto done;
    }

    ecs_entity_t scope = 0;
    ecs_entity_t cwd = box_path_cwd(world, session);
    if (box_path_split(world, cwd, to, &scope, &name)) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' is not a valid path", to);
        goto done;
    }

    ecs_entity_t existing = ecs_lookup_child(world, scope, name);
    if (existing && existing != e) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' already exists", to);
        goto done;
    }

    if (box_hierarchy_parent(world, e) != scope) {
        box_hierarchy_set_parent(world, e, scope);
    }

    ecs_set_name(world, e, name);
    out = box_path_str(world, e);

done:
    ecs_os_free(name);
    *(char**)result->ptr = out;
}

/** The add command: add a component or pair to an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_add(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    ecs_id_t id = 0;
    out = box_shell_id(world, expr, &id);
    if (out) {
        goto done;
    }

    ecs_add_id(world, e, id);
    out = box_path_str(world, e);

done:
    *(char**)result->ptr = out;
}

/** The remove command: remove a component or pair from an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_remove(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    ecs_id_t id = 0;
    out = box_shell_id(world, expr, &id);
    if (out) {
        goto done;
    }

    ecs_remove_id(world, e, id);
    out = box_path_str(world, e);

done:
    *(char**)result->ptr = out;
}

/** The set command: parse a value against a component type and assign it.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_set(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    const char *value = *(char**)argv[3].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    ecs_id_t id = 0;
    out = box_shell_id(world, expr, &id);
    if (out) {
        goto done;
    }

    ecs_entity_t type = ecs_get_typeid(world, id);
    const ecs_type_info_t *ti = type ? ecs_get_type_info(world, id) : NULL;
    if (!ti || !ti->size) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "'%s' has no value", expr);
        goto done;
    }

    ecs_value_t parsed = ecs_value_new(world, type);
    const void *current = ecs_get_id(world, e, id);
    if (current) {
        ecs_value_t existing = {
            .type = type,
            .ptr = ECS_CONST_CAST(void*, current)
        };
        ecs_value_copy(world, &parsed, &existing);
    }

    ecs_expr_eval_desc_t desc = { .type = type };
    int prev = ecs_log_set_level(-4);
    const char *end = ecs_expr_run(world, value, &parsed, &desc);
    ecs_log_set_level(prev);
    if (!end) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "invalid value '%s' for '%s'", value, expr);
    } else {
        ecs_set_id(world, e, id, (size_t)ti->size, parsed.ptr);
        out = box_path_str(world, e);
    }

    ecs_value_fini(world, &parsed);

done:
    *(char**)result->ptr = out;
}

/** The get command: print one component value or every component.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_get(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    if (!expr) {
        out = box_shell_values(world, e);
        goto done;
    }

    ecs_id_t id = 0;
    out = box_shell_id(world, expr, &id);
    if (out) {
        goto done;
    }

    ecs_entity_t type = ecs_get_typeid(world, id);
    const void *ptr = type ? ecs_get_id(world, e, id) : NULL;
    if (!ptr) {
        out = flecs_asprintf(
            BOX_COMMAND_ERROR_PREFIX "'%s' has no value of '%s'", path, expr);
        goto done;
    }

    out = ecs_ptr_to_expr(world, type, ptr);
    if (!out) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "cannot serialize '%s'", expr);
    }

done:
    *(char**)result->ptr = out;
}

/** The cat command: print every component of an entity with its value.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_cat(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    out = box_shell_values(world, e);

done:
    *(char**)result->ptr = out;
}

/** The clear command: remove every component of an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_clear(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t e = 0;
    if (!path) {
        TerminalSession *state = ecs_get_mut(world, session, TerminalSession);
        int32_t i;
        for (i = 0; i < BOX_COMMAND_LOG_MAX; i ++) {
            ecs_os_free(state->ring[i].line);
            ecs_os_free(state->ring[i].result);
            state->ring[i].line = NULL;
            state->ring[i].result = NULL;
        }

        state->count = 0;
        *(char**)result->ptr = ecs_os_strdup("");
        return;
    }

    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    out = box_path_str(world, e);
    ecs_clear(world, e);

done:
    *(char**)result->ptr = out;
}

/** The has command: test a component id or a query expression on an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_has(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    char *out = box_shell_target(world, session, path, &e);
    if (out) {
        goto done;
    }

    ecs_id_t id = 0;
    if (box_shell_is_id(expr)) {
        id = box_path_id(world, expr);
    }

    bool has = false;
    if (id) {
        has = ecs_has_id(world, e, id);
    } else {
        out = box_shell_match(world, e, expr, &has);
        if (out) {
            goto done;
        }
    }

    out = ecs_os_strdup(has ? "true" : "false");

done:
    *(char**)result->ptr = out;
}

/** The man command: print the doc brief, detail and link of an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_shell_man(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    ecs_entity_t cwd = box_path_cwd(world, session);
    ecs_entity_t e = 0;
    char *out = NULL;
    box_path_lookup(world, cwd, path, &e);
    if (!e) {
        e = ecs_lookup_child(world, box_command_scope(world), path);
    }

    if (!e) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX "unknown entity '%s'", path);
        goto done;
    }

    const char *doc[] = {
        ecs_doc_get_brief(world, e),
        ecs_doc_get_detail(world, e),
        ecs_doc_get_link(world, e)
    };

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        if (doc[i]) {
            ecs_strbuf_append(&buf, "%s\n", doc[i]);
        }
    }

    out = ecs_strbuf_get(&buf);

done:
    *(char**)result->ptr = out;
}

void box_shell_init(
    ecs_world_t *world)
{
    box_command_param_t path_opt[] = {{
        .name = "path", .type = ecs_id(ecs_string_t), .optional = true
    }};

    box_command_param_t path[] = {{
        .name = "path", .type = ecs_id(ecs_string_t)
    }};

    box_command_param_t move[] = {
        { .name = "from", .type = ecs_id(ecs_string_t) },
        { .name = "to", .type = ecs_id(ecs_string_t) }
    };

    box_command_param_t id[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) },
        { .name = "id", .type = ecs_id(ecs_string_t) }
    };

    box_command_param_t assign[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) },
        { .name = "component", .type = ecs_id(ecs_string_t) },
        { .name = "value", .type = ecs_id(ecs_string_t) }
    };

    box_command_param_t query[] = {
        { .name = "path", .type = ecs_id(ecs_string_t) },
        { .name = "component", .type = ecs_id(ecs_string_t), .optional = true }
    };

    box_command_register(world, "cd",
        "Change the current path of the session", path_opt, 1, box_shell_cd);

    box_command_register(world, "pwd",
        "Print the current path of the session", NULL, 0, box_shell_pwd);

    box_command_register(world, "ls",
        "List the names of the children of the current path or a path",
        path_opt, 1, box_shell_ls);

    box_command_register(world, "cat",
        "Print every component of an entity with its value", path, 1,
        box_shell_cat);

    box_command_register(world, "touch",
        "Create the entity of a path", path, 1, box_shell_touch);

    box_command_register(world, "rm",
        "Delete the entity of a path", path, 1, box_shell_rm);

    box_command_register(world, "mv",
        "Rename or reparent an entity", move, 2, box_shell_mv);

    box_command_register(world, "add",
        "Add a component or pair to an entity", id, 2, box_shell_add);

    box_command_register(world, "remove",
        "Remove a component or pair from an entity", id, 2, box_shell_remove);

    box_command_register(world, "set",
        "Assign a value to a component of an entity", assign, 3, box_shell_set);

    box_command_register(world, "get",
        "Print one component value or every component of an entity",
        query, 2, box_shell_get);

    box_command_register(world, "clear",
        "Clear the console or remove every component of an entity",
        path_opt, 1, box_shell_clear);

    box_command_register(world, "has",
        "Test a component id or a query expression on an entity", id, 2,
        box_shell_has);

    box_command_register(world, "man",
        "Print the documentation of an entity or a command", path, 1,
        box_shell_man);
}
