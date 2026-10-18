/**
 * @file ui/focus.c
 * @brief The keyboard focus: the Focused tag, FocusStatus, the checks that clear it and the Tab
 * walk through the Focusable entities of a FocusScope in 2D draw order.
 */

#include "ui.h"
#include "../render/render.h"

bool box_ui_can_focus(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return e && ecs_is_alive(world, e) && ecs_has_id(world, e, Focusable) &&
        !ecs_has_id(world, e, EcsDisabled) && !ecs_has_id(world, e, EcsPrefab);
}

ecs_entity_t box_ui_up(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_id_t id)
{
    int32_t depth = 0;
    while (e && depth < BOX_UI_DEPTH_MAX && !ecs_has_id(world, e, id)) {
        e = ecs_get_parent(world, e);
        depth ++;
    }

    return depth < BOX_UI_DEPTH_MAX ? e : 0;
}

/** Write the focus of the state to the FocusStatus singleton when it changed.
 *
 * @param world The world
 * @param st The module state
 */
static void box_ui_focus_publish(
    ecs_world_t *world,
    const box_ui_state_t *st)
{
    const FocusStatus *cur = ecs_singleton_get(world, FocusStatus);
    FocusStatus status = {
        .focused = st->focused,
        .previous = st->previous,
        .visible = st->visible
    };

    if (!cur || ecs_os_memcmp_t(cur, &status, FocusStatus)) {
        ecs_singleton_set_ptr(world, FocusStatus, &status);
    }
}

void box_ui_focus_set(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t e,
    bool visible)
{
    ecs_entity_t from = st->focused;
    bool from_visible = st->visible;
    if (from == e && (!e || from_visible == visible)) {
        goto done;
    }

    st->focused = e;
    st->visible = e && visible;
    if (from != e) {
        st->previous = from;
        st->space = 0;
        if (st->hold_source && st->hold_source != e) {
            box_ui_hold_release(world, st);
        }

        if (from && ecs_is_alive(world, from)) {
            ecs_remove_id(world, from, Focused);
        }

        if (e) {
            ecs_add_id(world, e, Focused);
        }
    }

    box_ui_focus_publish(world, st);
    if (from != e) {
        box_ui_shim_focus(world, st, from, from_visible, e, st->visible);
    }

done:
    return;
}

void box_ui_focus_check(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    if (st->focused && !box_ui_can_focus(world, st->focused)) {
        box_ui_focus_set(world, st, 0, false);
    }
}

/** Find the nearest FocusScope above an entity, the entity itself not included.
 *
 * @param world The world
 * @param e The entity
 * @return The scope, 0 when no ancestor is one
 */
static ecs_entity_t box_ui_scope_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t parent = e ? ecs_get_parent(world, e) : 0;

    return parent ? box_ui_up(world, parent, FocusScope) : 0;
}

/** Check whether an entity is below another one.
 *
 * @param world The world
 * @param e The entity
 * @param ancestor The other entity
 * @return Whether ancestor is a parent of e or of one of its ancestors
 */
static bool box_ui_below(
    const ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t ancestor)
{
    int32_t depth = 0;
    bool result = false;
    e = ecs_get_parent(world, e);
    while (e && !result && depth < BOX_UI_DEPTH_MAX) {
        result = e == ancestor;
        e = ecs_get_parent(world, e);
        depth ++;
    }

    return result;
}

/** Insert an entity into a list kept in 2D draw order.
 *
 * @param world The world
 * @param list The ecs_entity_t list
 * @param e The entity
 */
static void box_ui_order_insert(
    const ecs_world_t *world,
    ecs_vec_t *list,
    ecs_entity_t e)
{
    int32_t i = ecs_vec_count(list);
    ecs_entity_t *items;
    ecs_vec_append_t(NULL, list, ecs_entity_t);
    items = ecs_vec_first_t(list, ecs_entity_t);
    while (i > 0 && box_pick_drawn_after(world, items[i - 1], e)) {
        items[i] = items[i - 1];
        i --;
    }

    items[i] = e;
}

/** Collect the entities of a query that are below a scope, in 2D draw order.
 *
 * @param world The world
 * @param query The query
 * @param scope The scope, 0 for every entity of the query
 * @param scene Keep only entities of this scene, when scope is 0
 * @param list Receives the entities
 * @return Number of entities
 */
static int32_t box_ui_collect(
    ecs_world_t *world,
    ecs_query_t *query,
    ecs_entity_t scope,
    ecs_entity_t scene,
    ecs_vec_t *list)
{
    ecs_iter_t it = ecs_query_iter(world, query);
    int32_t i;
    ecs_vec_clear(list);
    while (ecs_query_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            bool keep = scope ? box_ui_below(world, e, scope) :
                box_render_scene_of(world, e) == scene;
            if (keep) {
                box_ui_order_insert(world, list, e);
            }
        }
    }

    return ecs_vec_count(list);
}

/** Find the Focusable entity Tab moves to inside a scope.
 *
 * @param world The world
 * @param st The module state
 * @param scope The scope
 * @param back Whether the focus moves back
 * @return The entity, 0 when the scope has no Focusable entity
 */
static ecs_entity_t box_ui_step_in(
    ecs_world_t *world,
    box_ui_state_t *st,
    ecs_entity_t scope,
    bool back)
{
    int32_t i, at = -1, count = box_ui_collect(world, st->focusables, scope, 0, &st->order);
    const ecs_entity_t *items = ecs_vec_first_t(&st->order, ecs_entity_t);
    ecs_entity_t result = 0;
    for (i = 0; i < count && at < 0; i ++) {
        at = items[i] == st->focused ? i : -1;
    }

    if (count && at < 0) {
        result = items[back ? count - 1 : 0];
    } else if (count) {
        result = items[(at + (back ? count - 1 : 1)) % count];
    }

    return result;
}

/** Find the first FocusScope with a Focusable entity in the scenes of the views of the surface
 * the cursor is on, the front view first, and the entity Tab moves to in it.
 *
 * @param world The world
 * @param st The module state
 * @param pst The picking state
 * @param back Whether the focus moves back
 * @return The entity, 0 when no scope has a Focusable entity
 */
static ecs_entity_t box_ui_step_first(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_pick_state_t *pst,
    bool back)
{
    ecs_entity_t views[BOX_PICK_VIEW_MAX];
    ecs_entity_t result = 0;
    ecs_vec_t scopes;
    int32_t v, s, count = box_pick_input_views(world, pst, views);
    ecs_vec_init_t(NULL, &scopes, ecs_entity_t, 0);
    for (v = 0; v < count && !result; v ++) {
        const View *view = ecs_get(world, views[v], View);
        ecs_entity_t camera = view ? view->camera : 0;
        if (!camera || !ecs_is_alive(world, camera)) {
            continue;
        }

        int32_t n = box_ui_collect(
            world, st->scopes, 0, box_render_scene_of(world, camera), &scopes);
        for (s = 0; s < n && !result; s ++) {
            result = box_ui_step_in(world, st, ecs_vec_get_t(&scopes, ecs_entity_t, s)[0], back);
        }
    }

    ecs_vec_fini_t(NULL, &scopes, ecs_entity_t);

    return result;
}

bool box_ui_focus_step(
    ecs_world_t *world,
    box_ui_state_t *st,
    box_pick_state_t *pst,
    bool back)
{
    ecs_entity_t scope = box_ui_scope_of(world, st->focused);
    ecs_entity_t next = 0;
    bool result = scope || ecs_query_is_true(st->scopes);
    if (scope) {
        next = box_ui_step_in(world, st, scope, back);
    } else if (result) {
        next = box_ui_step_first(world, st, pst, back);
    }

    if (next) {
        box_ui_focus_set(world, st, next, true);
    }

    return result;
}

/** Clear the focus when its entity loses Focusable or is deleted. The tasks that on.blur
 * resolved resume with the next progress of the tasks.
 *
 * The changes go through the main stage, which the remove or delete in progress merges when it
 * ends, because the entity must not move to another table while its observers run.
 *
 * @param it Iterator of the observer
 */
static void box_ui_focusable_on_remove(
    ecs_iter_t *it)
{
    box_ui_state_t *st = box_ui_state(it->world);
    ecs_world_t *world = ecs_get_stage(ecs_get_world(it->world), 0);
    int32_t i;
    if (!st || ecs_should_quit(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        if (it->entities[i] == st->focused) {
            box_ui_focus_set(world, st, 0, false);
        }
    }

done:
    return;
}

void box_ui_focus_import(
    ecs_world_t *world,
    box_ui_state_t *st)
{
    (void)st;
    ecs_add_pair(world, Focusable, EcsWith, Interactive);
    ecs_singleton_set(world, FocusStatus, {0});
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FocusableRemove" }),
        .query.terms = {{ .id = Focusable }},
        .events = { EcsOnRemove },
        .callback = box_ui_focusable_on_remove
    });
}
