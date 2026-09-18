/**
 * @file command/path.c
 * @brief Resolves the entity paths that command arguments are written with.
 */

#include "command_internal.h"
#include <string.h>

/** Test whether a path element is the parent token.
 *
 * @param path Position in the path where an element starts
 * @return true when the element is '..'
 */
static bool box_path_is_parent(
    const char *path)
{
    return path[0] == '.' && path[1] == '.' && (!path[2] || path[2] == '.');
}

/** Get the parent of a scope.
 *
 * @param world The world
 * @param scope The scope, 0 for the root
 * @return The parent scope, 0 for the root
 */
static ecs_entity_t box_path_parent(
    ecs_world_t *world,
    ecs_entity_t scope)
{
    ecs_entity_t result = 0;
    if (scope) {
        result = ecs_get_parent(world, scope);
    }

    return result;
}

/** Walk a path element by element and return the entity it resolves to.
 *
 * @param world The world
 * @param cwd Scope that relative paths resolve against
 * @param path The path
 * @param create Create the elements that do not exist yet
 * @param out Receives the entity the path resolves to, 0 for the root
 * @return 0 on success, -1 when an element does not exist
 */
static int box_path_walk(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    bool create,
    ecs_entity_t *out)
{
    int result = 0;
    ecs_entity_t scope = cwd;
    char *buffer = NULL;
    if (!path || !path[0] || !strcmp(path, "/")) {
        scope = 0;
        goto done;
    }

    buffer = ecs_os_strdup(path);
    char *p = buffer;
    if (p[0] == '.' && p[1] != '.') {
        scope = 0;
        p ++;
    }

    while (p[0]) {
        char *next;
        if (box_path_is_parent(p)) {
            scope = box_path_parent(world, scope);
            next = p + 2;
            if (next[0] == '.') {
                next ++;
            }
        } else {
            char *sep = strchr(p, '.');
            next = sep ? sep + 1 : p + strlen(p);
            if (sep) {
                sep[0] = '\0';
            }

            if (!p[0]) {
                scope = box_path_parent(world, scope);
            } else {
                ecs_entity_t child = ecs_lookup_child(world, scope, p);
                if (!child && create) {
                    child = ecs_entity(world, {
                        .name = p,
                        .parent = scope,
                        .sep = ""
                    });
                }

                if (!child) {
                    result = -1;
                    scope = 0;
                    goto done;
                }

                scope = child;
            }
        }

        p = next;
    }

done:
    ecs_os_free(buffer);
    out[0] = scope;

    return result;
}

ecs_entity_t box_path_cwd(
    ecs_world_t *world,
    ecs_entity_t session)
{
    const Terminal *t = ecs_get(world, session, Terminal);
    ecs_assert(t != NULL, ECS_INVALID_PARAMETER, "entity is not a terminal session");

    ecs_entity_t result = 0;
    if (t->cwd && t->cwd[0]) {
        result = ecs_lookup(world, t->cwd);
    }

    return result;
}

int box_path_lookup(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    ecs_entity_t *out)
{
    return box_path_walk(world, cwd, path, false, out);
}

ecs_entity_t box_path_ensure(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path)
{
    ecs_entity_t result = 0;
    box_path_walk(world, cwd, path, true, &result);

    return result;
}

int box_path_split(
    ecs_world_t *world,
    ecs_entity_t cwd,
    const char *path,
    ecs_entity_t *scope,
    char **name)
{
    int result = -1;
    scope[0] = 0;
    name[0] = NULL;
    if (!path || !path[0] || !strcmp(path, "/")) {
        goto done;
    }

    const char *sep = strrchr(path, '.');
    if (!sep) {
        scope[0] = cwd;
        name[0] = ecs_os_strdup(path);
        result = 0;
        goto done;
    }

    if (!sep[1]) {
        goto done;
    }

    ecs_size_t len = (ecs_size_t)(sep - path);
    char *head = ecs_os_malloc(len + 1);
    ecs_os_memcpy(head, path, len);
    head[len] = '\0';
    if (!box_path_walk(world, cwd, head, false, scope)) {
        name[0] = ecs_os_strdup(sep + 1);
        result = 0;
    }

    ecs_os_free(head);

done:
    return result;
}

char* box_path_str(
    ecs_world_t *world,
    ecs_entity_t e)
{
    char *result;
    if (!e) {
        result = ecs_os_strdup("/");
    } else {
        result = ecs_get_path(world, e);
    }

    return result;
}
