/**
 * @file editor/tree.c
 * @brief The tree view model: the visible rows of the entity hierarchy and their kinds.
 *
 * The rows are rebuilt when the selection or the expansion changed and every
 * BOX_EDITOR_TREE_INTERVAL frames otherwise, so the tree follows a scene that
 * changes without walking it every frame. Only expanded nodes are walked.
 */

#include "editor_internal.h"
#include <stdlib.h>
#include <string.h>

/** A child being sorted into its place in the tree */
typedef struct box_editor_child_t {
    ecs_entity_t entity; /**< The child */
    const char *name;    /**< Its name, NULL when it has none */
} box_editor_child_t;

/** Compare two children by name, nameless ones last by id.
 *
 * @param a The first child
 * @param b The second child
 * @return Negative when a sorts first, positive when b does, 0 when equal
 */
static int box_editor_child_compare(
    const void *a,
    const void *b)
{
    const box_editor_child_t *c1 = a;
    const box_editor_child_t *c2 = b;
    int result;
    if (c1->name && c2->name) {
        result = strcmp(c1->name, c2->name);
    } else if (c1->name || c2->name) {
        result = c1->name ? -1 : 1;
    } else {
        result = c1->entity < c2->entity ? -1 : (c1->entity > c2->entity);
    }

    return result;
}

void box_editor_children(
    ecs_world_t *world,
    ecs_entity_t parent,
    ecs_vec_t *out)
{
    EditorState *state = box_editor_state(world);
    ecs_iter_t it = parent ? ecs_children(world, parent) : ecs_query_iter(world, state->roots);
    ecs_iter_next_action_t next = parent ? ecs_children_next : ecs_query_next;
    while (next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, out, ecs_entity_t)[0] = it.entities[i];
        }
    }
}

/** Tell whether an entity has at least one child.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is a parent
 */
static bool box_editor_has_children(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_iter_t it = ecs_children(world, e);
    bool result = ecs_children_next(&it);
    if (result) {
        ecs_iter_fini(&it);
    }

    return result;
}

EditorKind box_editor_kind(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    EditorKind result = KindEntity;
    const EcsScript *script = ecs_get(world, e, EcsScript);
    const EcsComponent *component = ecs_get(world, e, EcsComponent);
    if (ecs_has_id(world, e, EcsModule)) {
        result = KindModule;
    } else if (script && script->template_) {
        result = KindTemplate;
    } else if (ecs_has_pair(world, e, ecs_id(EcsPoly), EcsSystem)) {
        result = KindSystem;
    } else if (ecs_has_pair(world, e, ecs_id(EcsPoly), EcsObserver)) {
        result = KindObserver;
    } else if (ecs_has_pair(world, e, ecs_id(EcsPoly), EcsQuery)) {
        result = KindQuery;
    } else if (component) {
        result = component->size ? KindComponent : KindTag;
    } else if (ecs_has_id(world, e, EcsPrefab)) {
        result = KindPrefab;
    } else if (box_editor_is_root(world, e)) {
        result = KindInstance;
    }

    return result;
}

void box_editor_expand(
    ecs_world_t *world,
    ecs_entity_t e,
    bool expanded)
{
    EditorState *state = box_editor_state(world);
    if (expanded) {
        ecs_map_ensure(&state->expanded, e)[0] = 1;
    } else {
        ecs_map_remove(&state->expanded, e);
    }

    state->tree_dirty = true;
}

void box_editor_reveal(
    ecs_world_t *world,
    ecs_entity_t e)
{
    EditorState *state = box_editor_state(world);
    ecs_entity_t cur = ecs_get_parent(world, e);
    while (cur) {
        ecs_map_ensure(&state->expanded, cur)[0] = 1;
        cur = ecs_get_parent(world, cur);
    }

    state->tree_dirty = true;
}

/** Release the strings of the rows and empty the vector.
 *
 * @param tree The model
 */
static void box_editor_tree_clear(
    EditorTree *tree)
{
    int32_t i, count = ecs_vec_count(&tree->rows);
    for (i = 0; i < count; i ++) {
        ecs_os_free(ecs_vec_get_t(&tree->rows, EditorNode, i)->name);
    }

    ecs_vec_clear(&tree->rows);
}

/** Append the rows of the children of an entity, recursing into expanded ones.
 *
 * @param world The world
 * @param state The module state
 * @param parent The entity whose children are appended, 0 for the root
 * @param depth Depth of the children
 * @param tree The model
 */
static void box_editor_tree_walk(
    ecs_world_t *world,
    EditorState *state,
    ecs_entity_t parent,
    int32_t depth,
    EditorTree *tree)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    ecs_vec_t entities;
    ecs_vec_init_t(NULL, &entities, ecs_entity_t, 0);
    box_editor_children(world, parent, &entities);

    int32_t i, count = ecs_vec_count(&entities);
    box_editor_child_t *children = ecs_os_malloc_n(box_editor_child_t, count + 1);
    for (i = 0; i < count; i ++) {
        children[i].entity = ecs_vec_get_t(&entities, ecs_entity_t, i)[0];
        children[i].name = ecs_get_name(world, children[i].entity);
    }

    qsort(children, (size_t)count, sizeof(box_editor_child_t), box_editor_child_compare);
    if (count > BOX_EDITOR_CHILDREN_MAX) {
        count = BOX_EDITOR_CHILDREN_MAX;
    }

    for (i = 0; i < count && ecs_vec_count(&tree->rows) < BOX_EDITOR_TREE_MAX; i ++) {
        ecs_entity_t e = children[i].entity;
        EditorNode *node = ecs_vec_append_t(NULL, &tree->rows, EditorNode);
        node->entity = e;
        node->name = children[i].name ? ecs_os_strdup(children[i].name) :
            flecs_asprintf("#%u", (uint32_t)e);
        node->depth = depth;
        node->kind = box_editor_kind(world, e);
        node->children = box_editor_has_children(world, e);
        node->expanded = node->children && ecs_map_get(&state->expanded, e) != NULL;
        node->selected = e == editor->selected;
        if (node->expanded) {
            box_editor_tree_walk(world, state, e, depth + 1, tree);
        }
    }

    ecs_os_free(children);
    ecs_vec_fini_t(NULL, &entities, ecs_entity_t);
}

void box_editor_tree_update(
    ecs_world_t *world,
    EditorState *state)
{
    state->tree_frames ++;
    if (!state->tree_dirty && state->tree_frames < BOX_EDITOR_TREE_INTERVAL) {
        goto done;
    }

    EditorTree *tree = ecs_get_mut(world, ecs_id(EditorTree), EditorTree);
    box_editor_tree_clear(tree);
    box_editor_tree_walk(world, state, 0, 0, tree);
    tree->version ++;
    ecs_modified(world, ecs_id(EditorTree), EditorTree);
    state->tree_dirty = false;
    state->tree_frames = 0;
done:
    return;
}
