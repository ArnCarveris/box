/**
 * @file command/cmd_component.c
 * @brief Component commands: add, remove, set, get and clear on entities of the shell.
 */

#include "command.h"
#include <string.h>

/** Parse a component or pair expression such as Position or (Likes, Bob).
 *
 * @param world The world
 * @param expr The expression
 * @return The id, 0 when the expression does not name one
 */
static ecs_id_t box_component_id(
    ecs_world_t *world,
    const char *expr)
{
    ecs_id_t result = 0;
    if (!expr[0] || strpbrk(expr, "|!?*$") || (expr[0] != '(' && strchr(expr, ','))) {
        goto done;
    }

    result = ecs_id_from_str(world, expr);
    if (!result && expr[0] != '(') {
        result = ecs_lookup_symbol(world, expr, true, false);
    }

done:
    return result;
}

/** Split a component path into the component and an optional member path.
 *
 * Position.x names member x of Position; the longest prefix that is a component wins.
 *
 * @param world The world
 * @param expr The component path
 * @param id Receives the component or pair
 * @param member Receives the member path, which the caller frees, NULL for the whole value
 * @return 0 on success, -1 when no prefix names a component
 */
static int box_component_parse(
    ecs_world_t *world,
    const char *expr,
    ecs_id_t *id,
    char **member)
{
    char *head = ecs_os_strdup(expr);
    int32_t i, depth = 0;
    int result = -1;
    member[0] = NULL;
    id[0] = box_component_id(world, expr);
    if (id[0]) {
        result = 0;
        goto done;
    }

    for (i = ecs_os_strlen(head) - 1; i > 0 && !id[0]; i --) {
        depth += (head[i] == ')') - (head[i] == '(');
        if (head[i] != '.' || depth) {
            continue;
        }

        head[i] = '\0';
        id[0] = box_component_id(world, head);
        if (id[0]) {
            member[0] = ecs_os_strdup(expr + i + 1);
            result = 0;
        }
    }

done:
    ecs_os_free(head);

    return result;
}

/** Point a value at a member of itself.
 *
 * @param world The world
 * @param value The value, changed to the member
 * @param member The member path, such as x or pos.x
 * @return 0 on success, -1 when the value has no such member
 */
static int box_component_member(
    ecs_world_t *world,
    ecs_value_t *value,
    const char *member)
{
    int result = -1;
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, value->type, value->ptr);
    if (!ecs_has(world, value->type, EcsStruct) || ecs_meta_push(&cur) ||
        ecs_meta_try_dotmember(&cur, member))
    {
        goto done;
    }

    value->type = ecs_meta_get_type(&cur);
    value->ptr = ecs_meta_get_ptr(&cur);
    result = 0;
done:
    return result;
}

/** Test whether an entity is used as a component or relationship.
 *
 * @param world The world
 * @param e The entity
 * @return true when an entity holds it as an id or as the first element of a pair
 */
static bool box_component_in_use(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_id_in_use(world, e) || ecs_id_in_use(world, ecs_pair(e, EcsWildcard));
}

/** Test whether an entity reaches another through a relationship.
 *
 * @param world The world
 * @param from The entity to start from
 * @param rel The relationship
 * @param to The entity to find
 * @return true when to is from or one of its targets, transitively
 */
static bool box_component_reaches(
    const ecs_world_t *world,
    ecs_entity_t from,
    ecs_entity_t rel,
    ecs_entity_t to)
{
    bool result = from == to;
    ecs_entity_t tgt;
    int32_t i = 0;
    while (!result && (tgt = ecs_get_target(world, from, rel, i ++))) {
        result = box_component_reaches(world, tgt, rel, to);
    }

    return result;
}

/** Test whether flecs accepts adding a component or pair, which it otherwise aborts on.
 *
 * @param world The world
 * @param id The component or pair
 * @return true when the id may be added
 */
static bool box_component_addable(
    const ecs_world_t *world,
    ecs_id_t id)
{
    bool result = ecs_id_is_valid(world, id) && !ecs_id_is_wildcard(id);
    ecs_entity_t rel, tgt, oneof;
    if (!result) {
        goto done;
    }

    if (!ECS_IS_PAIR(id)) {
        result = !ecs_has_id(world, id, EcsRelationship) && !ecs_has_id(world, id, EcsTarget);
        goto done;
    }

    rel = ecs_pair_first(world, id);
    tgt = ecs_pair_second(world, id);
    oneof = ecs_has_id(world, rel, EcsOneOf) ? rel : ecs_get_target(world, rel, EcsOneOf, 0);
    result = rel && tgt && !ecs_has_id(world, rel, EcsTarget) &&
        (!ecs_has_id(world, tgt, EcsRelationship) || ecs_has_id(world, rel, EcsTrait)) &&
        (!oneof || ecs_has_pair(world, tgt, EcsChildOf, oneof)) &&
        !(rel == EcsIsA && ecs_has_id(world, tgt, EcsFinal));
done:
    return result;
}

/** Check that a component or pair can be added to or removed from an entity.
 *
 * @param ctx The call context
 * @param result The result of the command, which gets the error
 * @param e The entity
 * @param id The component or pair
 * @param expr The component as the user wrote it
 * @param add true for add and set, false for remove
 * @return 0 when the change is allowed, -1 when the command failed
 */
static int box_component_check(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t e,
    ecs_id_t id,
    const char *expr,
    bool add)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t rel = ECS_IS_PAIR(id) ? ecs_pair_first(world, id) : (id & ECS_COMPONENT_MASK);
    ecs_entity_t tgt = ECS_IS_PAIR(id) ? ecs_pair_second(world, id) : 0;
    int ret = -1;
    if (add && !box_component_addable(world, id)) {
        box_command_error(ctx, result, "cannot add '%s'", expr);
    } else if (rel == EcsChildOf || id == ecs_id(EcsParent)) {
        box_command_error(ctx, result, "use mv to change the parent");
    } else if (add && tgt && ecs_has_id(world, rel, EcsTraversable) &&
        box_component_reaches(world, tgt, rel, e))
    {
        box_command_error(ctx, result, "'%s' creates a cycle", expr);
    } else if (((rel != EcsIsA && ecs_has_id(world, rel, EcsTrait)) ||
        id == ecs_id(EcsComponent)) && box_component_in_use(world, e))
    {
        box_command_error(ctx, result, "cannot change '%s' while it is in use", expr);
    } else {
        ret = 0;
    }

    return ret;
}

/** Resolve the entity and the component arguments of a command.
 *
 * @param ctx The call context
 * @param result The result of the command, which gets the error
 * @param argv The session, the entity path and the component path
 * @param access The access the command needs to the entity
 * @param e Receives the entity
 * @param id Receives the component or pair
 * @param member Receives the member path, which the caller frees, NULL for the whole value
 * @return 0 on success, -1 when the command failed
 */
static int box_component_args(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    const ecs_value_t *argv,
    box_shell_access_t access,
    ecs_entity_t *e,
    ecs_id_t *id,
    char **member)
{
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    int ret = -1;
    member[0] = NULL;
    if (box_shell_resolve(ctx, result, session, path, access, e)) {
        goto done;
    }

    if (box_component_parse(ctx->world, expr, id, member)) {
        box_command_error(ctx, result, "unknown component '%s'", expr);
        goto done;
    }

    ret = 0;
done:
    return ret;
}

/** The add command: add a component or pair to an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the entity path and the component
 * @param result Receives nothing on success
 */
static void box_component_add(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    ecs_id_t id = 0;
    char *member = NULL;
    if (box_component_args(ctx, result, argv, BoxShellWrite, &e, &id, &member)) {
        goto done;
    }

    if (member) {
        box_command_error(ctx, result, "unknown component '%s'", expr);
        goto done;
    }

    if (!box_component_check(ctx, result, e, id, expr, true)) {
        ecs_add_id(ctx->world, e, id);
    }

done:
    ecs_os_free(member);
}

/** The remove command: remove a component or pair from an entity.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the entity path and the component
 * @param result Receives nothing on success
 */
static void box_component_remove(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    const char *path = *(char**)argv[1].ptr;
    const char *expr = *(char**)argv[2].ptr;
    ecs_entity_t e = 0;
    ecs_id_t id = 0;
    char *member = NULL;
    if (box_component_args(ctx, result, argv, BoxShellWrite, &e, &id, &member)) {
        goto done;
    }

    if (member || !ecs_has_id(ctx->world, e, id)) {
        box_command_error(ctx, result, "'%s' has no '%s'", path, expr);
        goto done;
    }

    if (!box_component_check(ctx, result, e, id, expr, false)) {
        ecs_remove_id(ctx->world, e, id);
    }

done:
    ecs_os_free(member);
}

/** Get the type of the value of a component, failing the command for tags.
 *
 * @param ctx The call context
 * @param result The result of the command, which gets the error
 * @param id The component or pair
 * @param expr The component as the user wrote it
 * @return The type, 0 when the command failed
 */
static ecs_entity_t box_component_type(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_id_t id,
    const char *expr)
{
    ecs_entity_t type = ecs_get_typeid(ctx->world, id);
    const ecs_type_info_t *ti = type ? ecs_get_type_info(ctx->world, type) : NULL;
    if (!ti || !ti->size) {
        box_command_error(ctx, result, "'%s' has no value", expr);
        type = 0;
    } else if (ti->hooks.flags & (ECS_TYPE_HOOK_CTOR_ILLEGAL | ECS_TYPE_HOOK_COPY_ILLEGAL)) {
        box_command_error(ctx, result, "'%s' cannot be copied", expr);
        type = 0;
    }

    return type;
}

/** The set command: assign a flecs script expression to a component or one of its members.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the entity path, the component path and the value
 * @param result Receives nothing on success
 */
static void box_component_set(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *expr = *(char**)argv[2].ptr;
    const char *value = *(char**)argv[3].ptr;
    ecs_value_t v = {0}, target;
    ecs_entity_t e = 0, type;
    ecs_id_t id = 0;
    char *member = NULL;
    const char *end;
    const void *cur;
    if (box_component_args(ctx, result, argv, BoxShellWrite, &e, &id, &member) ||
        box_component_check(ctx, result, e, id, expr, true))
    {
        goto done;
    }

    type = box_component_type(ctx, result, id, expr);
    if (!type) {
        goto done;
    }

    cur = ecs_get_id(world, e, id);
    v = cur ? ecs_value_init(world, type, cur) : ecs_value_new(world, type);
    target = v;
    if (member && box_component_member(world, &target, member)) {
        box_command_error(ctx, result, "unknown member '%s'", expr);
        goto done;
    }

    end = ecs_expr_run(world, value, &target, &(ecs_expr_eval_desc_t){ .type = target.type });
    while (end && strchr(" \t\r\n", end[0]) && end[0]) {
        end ++;
    }

    if (!end || end[0]) {
        box_command_error(ctx, result, "invalid value '%s' for '%s'", value, expr);
        goto done;
    }

    ecs_set_id(world, e, id, (size_t)ecs_get_type_info(world, type)->size, v.ptr);
done:
    if (v.ptr) {
        ecs_value_fini(world, &v);
    }

    ecs_os_free(member);
}

/** List the components of an entity with their values.
 *
 * @param world The world
 * @param e The entity
 * @return One `component` or `component: value` line per component, the caller frees it
 */
static char* box_component_list(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i, count = type ? type->count : 0;
    for (i = 0; i < count; i ++) {
        ecs_id_t id = type->array[i];
        ecs_entity_t vtype = ecs_get_typeid(world, id);
        char *name, *value = NULL;
        if (ecs_id_match(id, ecs_pair(ecs_id(EcsIdentifier), EcsWildcard)) ||
            ecs_id_match(id, ecs_pair(EcsChildOf, EcsWildcard)) || id == ecs_id(EcsParent))
        {
            continue;
        }

        if (vtype && ecs_has(world, vtype, EcsTypeSerializer)) {
            value = ecs_ptr_to_expr(world, vtype, ecs_get_id(world, e, id));
        }

        name = ecs_id_str(world, id);
        if (ecs_strbuf_written(&buf)) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        ecs_strbuf_appendstr(&buf, name);
        if (value) {
            ecs_strbuf_append(&buf, ": %s", value);
        }

        ecs_os_free(name);
        ecs_os_free(value);
    }

    return ecs_strbuf_get(&buf);
}

/** The get command: print a component value, a member value or every component.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the entity path and the optional component path
 * @param result Receives the value as a flecs script expression
 */
static void box_component_get(
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
    ecs_id_t id = 0;
    ecs_value_t v;
    char *member = NULL, *out;
    if (!expr) {
        if (!box_shell_resolve(ctx, result, session, path, BoxShellRead, &e)) {
            *(char**)result->ptr = box_component_list(world, e);
        }

        goto done;
    }

    if (box_component_args(ctx, result, argv, BoxShellRead, &e, &id, &member)) {
        goto done;
    }

    if (!ecs_has_id(world, e, id)) {
        box_command_error(ctx, result, "'%s' has no '%s'", path, expr);
        goto done;
    }

    v.type = box_component_type(ctx, result, id, expr);
    if (!v.type) {
        goto done;
    }

    v.ptr = ECS_CONST_CAST(void*, ecs_get_id(world, e, id));
    if (member && box_component_member(world, &v, member)) {
        box_command_error(ctx, result, "unknown member '%s'", expr);
        goto done;
    }

    out = ecs_has(world, v.type, EcsTypeSerializer) ? ecs_ptr_to_expr(world, v.type, v.ptr) : NULL;
    if (!out) {
        box_command_error(ctx, result, "cannot print '%s'", expr);
        goto done;
    }

    *(char**)result->ptr = out;
done:
    ecs_os_free(member);
}

/** The clear command: remove every component of an entity but its name and parent.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the entity path
 * @param result Receives nothing on success
 */
static void box_component_clear(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    ecs_entity_t session = *(ecs_entity_t*)argv[0].ptr;
    const char *path = *(char**)argv[1].ptr;
    const ecs_type_t *type;
    ecs_vec_t ids = {0};
    ecs_entity_t e = 0;
    int32_t i;
    if (box_shell_resolve(ctx, result, session, path, BoxShellDelete, &e)) {
        goto done;
    }

    if (box_component_in_use(world, e)) {
        box_command_error(ctx, result, "cannot clear '%s' while it is in use", path);
        goto done;
    }

    type = ecs_get_type(world, e);
    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (!ecs_id_match(id, ecs_pair(ecs_id(EcsIdentifier), EcsWildcard)) &&
            !ecs_id_match(id, ecs_pair(EcsChildOf, EcsWildcard)) && id != ecs_id(EcsParent))
        {
            ecs_vec_append_t(NULL, &ids, ecs_id_t)[0] = id;
        }
    }

    for (i = 0; i < ecs_vec_count(&ids); i ++) {
        ecs_remove_id(world, e, ecs_vec_get_t(&ids, ecs_id_t, i)[0]);
    }

done:
    ecs_vec_fini_t(NULL, &ids, ecs_id_t);
}

void box_component_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "add",
        .brief = "Add a component or pair to an entity",
        .params = {
            { .name = "entity", .type = ecs_id(ecs_string_t) },
            { .name = "component", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_component_add
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "remove",
        .brief = "Remove a component or pair from an entity",
        .params = {
            { .name = "entity", .type = ecs_id(ecs_string_t) },
            { .name = "component", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_component_remove
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "set",
        .brief = "Assign a flecs script value to a component or member like Position.x",
        .params = {
            { .name = "entity", .type = ecs_id(ecs_string_t) },
            { .name = "component", .type = ecs_id(ecs_string_t) },
            { .name = "value", .type = ecs_id(ecs_string_t), .rest = true }
        },
        .callback = box_component_set
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "get",
        .brief = "Print a component or member value, every component when omitted",
        .params = {
            { .name = "entity", .type = ecs_id(ecs_string_t) },
            { .name = "component", .type = ecs_id(ecs_string_t), .optional = true }
        },
        .callback = box_component_get
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "clear",
        .brief = "Remove every component of an entity but its name and parent",
        .params = {{ .name = "entity", .type = ecs_id(ecs_string_t) }},
        .callback = box_component_clear
    });
}
