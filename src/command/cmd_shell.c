/**
 * @file command/cmd_shell.c
 * @brief The entity shell: cd, ls, touch, rm, mv and man on paths relative to the session.
 */

#include "command.h"
#include <stdlib.h>
#include <string.h>

/** Most children ls prints before it counts the rest */
#define BOX_SHELL_LS_MAX (256)

/** A child that ls prints */
typedef struct box_shell_child_t {
    const char *name;   /**< Name of the child, NULL when it has none */
    ecs_entity_t e;     /**< The child */
} box_shell_child_t;

/** Labels of the flecs.doc descriptions that man prints */
static const char *box_shell_doc_labels[] = {
    "name", "brief", "detail", "link", "color", "uuid"
};

/** Test whether an entity is flecs itself or lives in its scope.
 *
 * @param world The world
 * @param e The entity
 * @return true for a builtin entity
 */
static bool box_shell_builtin(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur && !result) {
        result = cur == EcsFlecs;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Test whether an entity is a module, a session or lives in the scope of one.
 *
 * @param world The world
 * @param e The entity
 * @return true when changing or deleting the entity could break a module or a session
 */
static bool box_shell_protected(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    ecs_entity_t cur = e;
    while (cur && !result) {
        result = ecs_has_id(world, cur, EcsModule) || ecs_has(world, cur, Terminal);
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Test whether deleting an entity or one of its children trips an (OnDelete, Panic) policy.
 *
 * @param world The world
 * @param e The entity
 * @return true when the delete would panic
 */
static bool box_shell_panics(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = ecs_has_pair(world, e, EcsOnDelete, EcsPanic);
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count && !result; i ++) {
            result = box_shell_panics(world, it.entities[i]);
        }
    }

    return result;
}

/** Walk the root and parent prefixes of a path.
 *
 * A leading / starts at the root, every ../ goes up one level and ./ stays.
 *
 * @param world The world
 * @param cwd The current entity of the session, 0 for the root
 * @param path The path
 * @param scope Receives the scope that the rest of the path is relative to
 * @return The rest of the path
 */
static const char* box_shell_prefix(
    const ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    ecs_entity_t *scope)
{
    const char *p = path;
    scope[0] = cwd;
    if (p[0] == '/') {
        scope[0] = 0;
        p ++;
    }

    while (p[0] == '.' && (p[1] == '/' || (p[1] == '.' && (!p[2] || p[2] == '/')))) {
        if (p[1] == '.') {
            scope[0] = scope[0] ? ecs_get_parent(world, scope[0]) : 0;
            p ++;
        }

        p += p[1] ? 2 : 1;
    }

    return p;
}

/** Look up a dotted path below a scope.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @param rest The path, empty or . for the scope itself
 * @param out Receives the entity, 0 for the root
 * @return 0 when the path exists, -1 otherwise
 */
static int box_shell_find(
    const ecs_world_t *world,
    ecs_entity_t scope,
    const char *rest,
    ecs_entity_t *out)
{
    int result = 0;
    out[0] = scope;
    if (!rest[0] || !strcmp(rest, ".")) {
        goto done;
    }

    out[0] = ecs_lookup_path_w_sep(world, scope, rest, ".", NULL, false);
    if (!out[0] || ecs_id_is_wildcard(out[0]) || out[0] == EcsThis ||
        out[0] == EcsVariable)
    {
        out[0] = 0;
        result = -1;
    }

done:
    return result;
}

/** Test whether a path names an entity that touch or mv may create.
 *
 * @param rest The path after its prefixes
 * @return true when every element is a valid name
 */
static bool box_shell_valid_name(
    const char *rest)
{
    bool result = rest[0] && rest[0] != '.' && rest[0] != '#' && !strpbrk(rest, "*$/") &&
        strcmp(rest, "_") && !strstr(rest, "..") && rest[ecs_os_strlen(rest) - 1] != '.';

    return result;
}

int box_shell_resolve(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    const char *path,
    box_shell_access_t access,
    ecs_entity_t *out)
{
    ecs_world_t *world = ctx->world;
    const char *p = path ? path : ".";
    ecs_entity_t scope = 0;
    const char *rest = box_shell_prefix(world, box_terminal_cwd(world, session), p, &scope);
    int ret = -1;
    if (box_shell_find(world, scope, rest, out)) {
        box_command_error(ctx, result, "no entity '%s'", p);
    } else if (access != BoxShellScope && !out[0]) {
        box_command_error(ctx, result, "'%s' is the root", p);
    } else if (access >= BoxShellWrite && box_shell_builtin(world, out[0])) {
        box_command_error(ctx, result, "'%s' is builtin", p);
    } else if (access == BoxShellDelete && box_shell_protected(world, out[0])) {
        box_command_error(ctx, result, "'%s' belongs to a module or session", p);
    } else {
        ret = 0;
    }

    return ret;
}

/** The cd command: change the current entity of the session, the root when omitted.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives the new absolute path
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
    char *cwd = NULL;
    Terminal *t;
    if (box_shell_resolve(ctx, result, session, path ? path : "/", BoxShellScope, &e)) {
        goto done;
    }

    cwd = e ? ecs_get_path(world, e) : NULL;
    if (cwd && ecs_lookup_path_w_sep(world, 0, cwd, ".", NULL, false) != e) {
        box_command_error(ctx, result, "cannot enter '%s'", path);
        goto done;
    }

    *(char**)result->ptr = flecs_asprintf("/%s", cwd ? cwd : "");
    t = ecs_get_mut(world, session, Terminal);
    ecs_os_free(t->cwd);
    t->cwd = cwd;
    cwd = NULL;
    ecs_modified(world, session, Terminal);
done:
    ecs_os_free(cwd);
}

/** Order listed children by name, unnamed children last by id.
 *
 * @param a The first child
 * @param b The second child
 * @return Negative when a sorts first, positive when b does
 */
static int box_shell_compare(
    const void *a,
    const void *b)
{
    const box_shell_child_t *c1 = a;
    const box_shell_child_t *c2 = b;
    int result;
    if (c1->name && c2->name) {
        result = strcmp(c1->name, c2->name);
    } else if (c1->name || c2->name) {
        result = c1->name ? -1 : 1;
    } else {
        result = (c1->e > c2->e) - (c1->e < c2->e);
    }

    return result;
}

/** Collect the children of a scope, or the named root entities for the root.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @param out Receives the box_shell_child_t of every child
 */
static void box_shell_collect(
    ecs_world_t *world,
    ecs_entity_t scope,
    ecs_vec_t *out)
{
    ecs_query_t *q = NULL;
    ecs_iter_t it;
    int32_t i;
    if (scope) {
        it = ecs_children(world, scope);
    } else {
        q = ecs_query(world, {
            .terms = {
                { .id = ecs_pair(ecs_id(EcsIdentifier), EcsName) },
                { .id = ecs_pair(EcsChildOf, EcsWildcard), .oper = EcsNot },
                { .id = ecs_id(EcsParent), .oper = EcsNot }
            },
            .flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled
        });
        it = ecs_query_iter(world, q);
    }

    while (scope ? ecs_children_next(&it) : ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            box_shell_child_t *c = ecs_vec_append_t(NULL, out, box_shell_child_t);
            c->e = it.entities[i];
            c->name = ecs_get_name(world, c->e);
        }
    }

    if (q) {
        ecs_query_fini(q);
    }

    qsort(ecs_vec_first(out), (size_t)ecs_vec_count(out), sizeof(box_shell_child_t),
        box_shell_compare);
}

/** The ls command: list the children of the current entity or of a path.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives one name per line, #id for unnamed children
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
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_vec_t children = {0};
    ecs_entity_t scope = 0;
    int32_t i, count;
    if (box_shell_resolve(ctx, result, session, path, BoxShellScope, &scope)) {
        goto done;
    }

    box_shell_collect(world, scope, &children);
    count = ecs_vec_count(&children);
    for (i = 0; i < count && i < BOX_SHELL_LS_MAX; i ++) {
        box_shell_child_t *c = ecs_vec_get_t(&children, box_shell_child_t, i);
        if (i) {
            ecs_strbuf_appendch(&buf, '\n');
        }

        if (c->name) {
            ecs_strbuf_appendstr(&buf, c->name);
        } else {
            ecs_strbuf_append(&buf, "#%u", (uint32_t)c->e);
        }
    }

    if (count > BOX_SHELL_LS_MAX) {
        ecs_strbuf_append(&buf, "\n... %d more", count - BOX_SHELL_LS_MAX);
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    ecs_vec_fini_t(NULL, &children, box_shell_child_t);
}

/** The touch command: create the entity of a path and its missing parents.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives nothing on success
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
    ecs_entity_t scope = 0, e = 0;
    const char *rest = box_shell_prefix(world, box_terminal_cwd(world, session), path, &scope);
    if (!box_shell_valid_name(rest)) {
        box_command_error(ctx, result, "invalid path '%s'", path);
        goto done;
    }

    if (box_shell_builtin(world, scope)) {
        box_command_error(ctx, result, "'%s' is builtin", path);
        goto done;
    }

    if (box_shell_find(world, scope, rest, &e)) {
        ecs_entity(world, { .name = rest, .parent = scope });
    }

done:
    return;
}

/** The rm command: delete the entity of a path and its children.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives nothing on success
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
    if (box_shell_resolve(ctx, result, session, path, BoxShellDelete, &e)) {
        goto done;
    }

    if (box_shell_panics(world, e)) {
        box_command_error(ctx, result, "'%s' may not be deleted", path);
        goto done;
    }

    ecs_delete(world, e);
done:
    return;
}

/** Find the parent and the name that mv moves an entity to.
 *
 * An existing destination is a parent the entity moves into with its name.
 *
 * @param ctx The call context
 * @param result The result of the command
 * @param session The session entity
 * @param e The entity that moves
 * @param to The destination path
 * @param parent Receives the new parent, 0 for the root
 * @param name Receives the new name, which the caller frees, NULL for no name
 * @return 0 on success, -1 when the command failed
 */
static int box_shell_destination(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result,
    ecs_entity_t session,
    ecs_entity_t e,
    const char *to,
    ecs_entity_t *parent,
    char **name)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t scope = 0;
    const char *rest = box_shell_prefix(world, box_terminal_cwd(world, session), to, &scope);
    const char *sep = strrchr(rest, '.');
    char *head = NULL;
    int ret = -1;
    if (!box_shell_find(world, scope, rest, parent)) {
        if (parent[0] == e) {
            parent[0] = ecs_get_parent(world, e);
        }

        name[0] = ecs_os_strdup(ecs_get_name(world, e));
        ret = 0;
        goto done;
    }

    if (!box_shell_valid_name(rest)) {
        box_command_error(ctx, result, "invalid path '%s'", to);
        goto done;
    }

    if (sep) {
        head = ecs_os_strdup(rest);
        head[sep - rest] = '\0';
        if (box_shell_find(world, scope, head, &scope)) {
            box_command_error(ctx, result, "no entity '%s'", head);
            goto done;
        }
    }

    parent[0] = scope;
    name[0] = ecs_os_strdup(sep ? sep + 1 : rest);
    ret = 0;
done:
    ecs_os_free(head);

    return ret;
}

/** Change the parent of an entity, keeping the kind of parent relationship it uses.
 *
 * @param world The world
 * @param e The entity
 * @param parent The new parent, 0 for the root
 */
static void box_shell_reparent(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t parent)
{
    if (ecs_owns(world, e, EcsParent)) {
        if (parent) {
            ecs_set(world, e, EcsParent, { .value = parent });
        } else {
            ecs_remove(world, e, EcsParent);
        }
    } else if (parent) {
        ecs_add_pair(world, e, EcsChildOf, parent);
    } else {
        ecs_remove_pair(world, e, EcsChildOf, EcsWildcard);
    }
}

/** The mv command: rename an entity or move it to another parent.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session, the source path and the destination path
 * @param result Receives nothing on success
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
    ecs_entity_t e = 0, parent = 0, cur, other;
    char *name = NULL;
    if (box_shell_resolve(ctx, result, session, from, BoxShellDelete, &e)) {
        goto done;
    }

    if (box_shell_destination(ctx, result, session, e, to, &parent, &name)) {
        goto done;
    }

    if (box_shell_builtin(world, parent) || box_shell_protected(world, parent)) {
        box_command_error(ctx, result, "cannot move into '%s'", to);
        goto done;
    }

    for (cur = parent; cur; cur = ecs_get_parent(world, cur)) {
        if (cur == e) {
            box_command_error(ctx, result, "cannot move '%s' into itself", from);
            goto done;
        }
    }

    other = name ? ecs_lookup_child(world, parent, name) : 0;
    if (other && other != e) {
        box_command_error(ctx, result, "'%s' exists", to);
        goto done;
    }

    if (ecs_get_parent(world, e) != parent) {
        ecs_set_name(world, e, NULL);
        box_shell_reparent(world, e, parent);
    }

    ecs_set_name(world, e, name);
done:
    ecs_os_free(name);
}

/** The man command: print the flecs.doc descriptions of an entity or a command.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session and the path
 * @param result Receives one label: text line per description, a usage line for commands
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
    const ecs_entity_t kinds[] = {
        EcsName, EcsDocBrief, EcsDocDetail, EcsDocLink, EcsDocColor, EcsDocUuid
    };
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_entity_t scope = 0, e = 0;
    const char *rest = box_shell_prefix(world, box_terminal_cwd(world, session), path, &scope);
    int32_t i;
    if (box_shell_find(world, scope, rest, &e) || !e) {
        e = ecs_lookup_path_w_sep(
            world, ecs_lookup(world, BOX_COMMAND_SCOPE), path, ".", NULL, false);
    }

    if (!e) {
        box_command_error(ctx, result, "no entity '%s'", path);
        goto done;
    }

    if (ecs_has(world, e, CommandInfo) && ecs_has(world, e, EcsScriptFunction)) {
        box_command_usage(world, e, &buf);
    }

    for (i = 0; i < (int32_t)(sizeof(kinds) / sizeof(kinds[0])); i ++) {
        const EcsDocDescription *d = ecs_get_pair(world, e, EcsDocDescription, kinds[i]);
        if (d && d->value) {
            if (ecs_strbuf_written(&buf)) {
                ecs_strbuf_appendch(&buf, '\n');
            }

            ecs_strbuf_append(&buf, "%s: %s", box_shell_doc_labels[i], d->value);
        }
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

void box_shell_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "cd",
        .brief = "Change the current entity, the root when omitted",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t), .optional = true }},
        .callback = box_shell_cd
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "ls",
        .brief = "List the children of the current entity or a path",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t), .optional = true }},
        .callback = box_shell_ls
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "touch",
        .brief = "Create an entity and its missing parents",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t) }},
        .callback = box_shell_touch
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "rm",
        .brief = "Delete an entity and its children",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t) }},
        .callback = box_shell_rm
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "mv",
        .brief = "Rename an entity, or move it into an existing entity",
        .params = {
            { .name = "from", .type = ecs_id(ecs_string_t) },
            { .name = "to", .type = ecs_id(ecs_string_t) }
        },
        .callback = box_shell_mv
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "man",
        .brief = "Show the flecs.doc descriptions of an entity or command",
        .params = {{ .name = "path", .type = ecs_id(ecs_string_t) }},
        .callback = box_shell_man
    });

    box_component_init(world);
}
