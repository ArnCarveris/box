/**
 * @file editor/select.c
 * @brief Selection of the editor: instance roots, opened instances and what they hide.
 *
 * A click on a child of a prefab or template instance selects the root of the
 * instance, until that instance is opened. An opened instance is not selected
 * itself, only what it contains, so its bounds never stand in for a child.
 *
 * Opening hides every sibling of the instance and of its ancestors except the
 * cameras and the lights, so the instance is seen on its own.
 */

#include "editor_internal.h"

bool box_editor_is_root(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    if (ecs_has(world, e, RootBounds)) {
        result = true;
        goto done;
    }

    ecs_entity_t base = ecs_get_target(world, e, EcsIsA, 0);
    if (base && ecs_has_id(world, base, EcsPrefab)) {
        result = true;
        goto done;
    }

    const ecs_type_t *type = ecs_get_type(world, e);
    int32_t i;
    for (i = 0; type && i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (ECS_IS_PAIR(id) || !ecs_is_alive(world, id)) {
            continue;
        }

        const EcsScript *script = ecs_get(world, id, EcsScript);
        if (script && script->template_) {
            result = true;
            break;
        }
    }

done:
    return result;
}

/** Tell whether an entity is the opened instance or lies inside it.
 *
 * @param world The world
 * @param e The entity
 * @param opened The opened instance
 * @return true when the entity is the instance or one of its descendants
 */
static bool box_editor_inside(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t opened)
{
    ecs_entity_t cur = e;
    bool result = false;
    while (cur && !result) {
        result = cur == opened;
        cur = ecs_get_parent(world, cur);
    }

    return result;
}

ecs_entity_t box_editor_resolve(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Editor *editor = ecs_singleton_get(world, Editor);
    ecs_entity_t opened = editor->opened;
    ecs_entity_t result = e;
    ecs_entity_t cur = e;
    while (cur && ecs_is_alive(world, cur)) {
        if (cur == opened) {
            break;
        }

        if (box_editor_is_root(world, cur)) {
            result = cur;
        }

        cur = ecs_get_parent(world, cur);
    }

    if (opened && result == opened && e != opened &&
        box_editor_inside(world, e, opened))
    {
        result = e;
    }

    if (opened && result == opened) {
        result = 0;
    }

    return result;
}

void box_editor_describe(
    ecs_world_t *world)
{
    Editor *editor = box_editor_get(world);
    ecs_entity_t e = editor->selected;
    bool alive = e && ecs_is_alive(world, e);
    editor->instance = alive && box_editor_is_root(world, e) && e != editor->opened;
    editor->spline = alive && ecs_has(world, e, Spline);
    editor->transform = alive &&
        box_editor_editable(world, e, ecs_id(Position)) &&
        box_editor_editable(world, e, ecs_id(Rotation)) &&
        box_editor_editable(world, e, ecs_id(Scale));
    if (!editor->spline) {
        editor->point = -1;
    }

    box_editor_modified(world);
}

ecs_entity_t box_editor_select(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = e && ecs_is_alive(world, e) ? box_editor_resolve(world, e) : 0;
    EditorState *state = box_editor_state(world);
    Editor *editor = box_editor_get(world);
    state->grab.active = false;
    if (editor->selected != result) {
        editor->selected = result;
        editor->point = -1;
        state->tree_dirty = true;
        if (result) {
            box_editor_reveal(world, result);
        }
    }

    box_editor_describe(world);

    return result;
}

/** Tell whether an entity lights the scene, so opening an instance keeps it.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity carries a light or the atmosphere
 */
static bool box_editor_is_light(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, DirectionalLight) || ecs_has(world, e, CelestialLight) ||
        ecs_has(world, e, PointLight) || ecs_has(world, e, SpotLight) ||
        ecs_has(world, e, EnvironmentLight) || ecs_has(world, e, Atmosphere);
}

/** Hide one sibling of an opened instance, remembering it on the level.
 *
 * Cameras and lights stay, so the opened instance is seen through the same
 * camera and under the same light as the scene.
 *
 * @param world The world
 * @param e The sibling
 * @param level The level of the opened instance
 */
static void box_editor_hide(
    ecs_world_t *world,
    ecs_entity_t e,
    box_editor_level_t *level)
{
    if (ecs_has_id(world, e, Hidden) || ecs_has_id(world, e, EcsModule) ||
        ecs_has_id(world, e, EcsPrefab) || ecs_has(world, e, EcsComponent) ||
        ecs_has(world, e, EcsScript) || ecs_has(world, e, Camera) ||
        box_editor_is_light(world, e))
    {
        goto done;
    }

    ecs_add_id(world, e, Hidden);
    ecs_vec_append_t(NULL, &level->hidden, ecs_entity_t)[0] = e;
done:
    return;
}

/** Hide the siblings of an entity and of every ancestor up to a scope.
 *
 * @param world The world
 * @param e The entity that stays visible
 * @param stop Ancestor the walk stops at, 0 for the root of the world
 * @param level The level of the opened instance
 */
static void box_editor_hide_around(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t stop,
    box_editor_level_t *level)
{
    ecs_vec_t siblings;
    ecs_vec_init_t(NULL, &siblings, ecs_entity_t, 0);
    ecs_entity_t cur = e;
    while (cur && cur != stop) {
        ecs_entity_t parent = ecs_get_parent(world, cur);
        box_editor_children(world, parent, &siblings);
        cur = parent;
    }

    int32_t i, count = ecs_vec_count(&siblings);
    for (i = 0; i < count; i ++) {
        ecs_entity_t sibling = ecs_vec_get_t(&siblings, ecs_entity_t, i)[0];
        if (!box_editor_inside(world, e, sibling)) {
            box_editor_hide(world, sibling, level);
        }
    }

    ecs_vec_fini_t(NULL, &siblings, ecs_entity_t);
}

bool box_editor_open(
    ecs_world_t *world,
    ecs_entity_t e)
{
    bool result = false;
    Editor *editor = box_editor_get(world);
    if (!e || !ecs_is_alive(world, e) || e == editor->opened ||
        !box_editor_is_root(world, e))
    {
        goto done;
    }

    if (editor->opened && !box_editor_inside(world, e, editor->opened)) {
        goto done;
    }

    EditorState *state = box_editor_state(world);
    box_editor_level_t *level = ecs_vec_append_t(
        NULL, &state->levels, box_editor_level_t);
    level->instance = e;
    ecs_vec_init_t(NULL, &level->hidden, ecs_entity_t, 0);
    box_editor_hide_around(world, e, editor->opened, level);

    editor = box_editor_get(world);
    editor->opened = e;
    editor->depth = ecs_vec_count(&state->levels);
    editor->selected = 0;
    editor->point = -1;
    state->tree_dirty = true;
    box_editor_reveal(world, e);
    box_editor_describe(world);
    result = true;
done:
    return result;
}

/** Show what the innermost level hid and drop the level.
 *
 * @param world The world
 * @param state The module state
 */
static void box_editor_pop(
    ecs_world_t *world,
    EditorState *state)
{
    int32_t last = ecs_vec_count(&state->levels) - 1;
    box_editor_level_t *level = ecs_vec_get_t(&state->levels, box_editor_level_t, last);
    int32_t i, count = ecs_vec_count(&level->hidden);
    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_vec_get_t(&level->hidden, ecs_entity_t, i)[0];
        if (ecs_is_alive(world, e)) {
            ecs_remove_id(world, e, Hidden);
        }
    }

    ecs_vec_fini_t(NULL, &level->hidden, ecs_entity_t);
    ecs_vec_remove_last(&state->levels);
}

bool box_editor_close(
    ecs_world_t *world)
{
    EditorState *state = box_editor_state(world);
    bool result = false;
    if (!ecs_vec_count(&state->levels)) {
        goto done;
    }

    ecs_entity_t closed = ecs_vec_last_t(&state->levels, box_editor_level_t)->instance;
    box_editor_pop(world, state);
    Editor *editor = box_editor_get(world);
    int32_t count = ecs_vec_count(&state->levels);
    editor->opened = count ?
        ecs_vec_last_t(&state->levels, box_editor_level_t)->instance : 0;
    editor->depth = count;
    editor->selected = ecs_is_alive(world, closed) ? closed : 0;
    editor->point = -1;
    state->tree_dirty = true;
    box_editor_describe(world);
    result = true;
done:
    return result;
}

void box_editor_close_all(
    ecs_world_t *world)
{
    EditorState *state = box_editor_state(world);
    while (ecs_vec_count(&state->levels)) {
        box_editor_pop(world, state);
    }

    Editor *editor = box_editor_get(world);
    editor->opened = 0;
    editor->depth = 0;
}
