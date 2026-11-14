/**
 * @file picking/picking.c
 * @brief Registers box.picking and sends the pointer events of a frame to what the cursor picks.
 *
 * box.input hands every event of a frame to the router of this module, which sends them in
 * order in PreUpdate, picking what the last frame placed, so handlers act before the layout.
 * The ActionTriggers of the delivered events reach the actions in the next ActionUpdate.
 *
 * The Cursor on the path of the target sets the pointer cursor shape once per frame.
 */

#define BOX_PICKING_IMPL
#include "picking.h"

ECS_COMPONENT_DECLARE(PickingState);
ECS_TAG_DECLARE(Interactive);
ECS_TAG_DECLARE(InputBlocker);
ECS_TAG_DECLARE(Hovered);
ECS_TAG_DECLARE(Pressed);
ECS_TAG_DECLARE(PickThrough);

/** Release the state of the module with the futures still waiting for a wheel movement.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_pick_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    PickingState *ps = ptr;
    int32_t i, w;
    (void)ti;
    for (i = 0; i < count; i ++) {
        box_pick_state_t *st = ps[i].state;
        if (!st) {
            continue;
        }

        for (w = 0; w < ecs_vec_count(&st->waiters); w ++) {
            ecs_script_future_release(
                ecs_vec_get_t(&st->waiters, box_pick_waiter_t, w)->future);
        }

        ecs_vec_fini_t(NULL, &st->emits, box_input_emit_t);
        ecs_vec_fini_t(NULL, &st->candidates, box_pick_candidate_t);
        ecs_vec_fini_t(NULL, &st->waiters, box_pick_waiter_t);
        ecs_vec_fini_t(NULL, &st->roots, ecs_entity_t);
        box_pick_meshes_fini(st);
        ecs_os_free(st);
    }
}

/** Queue an event of the frame until the picks of the frame can run.
 *
 * @param world The world
 * @param emit The event
 * @param ctx The module state
 */
static void box_pick_route(
    ecs_world_t *world,
    const box_input_emit_t *emit,
    void *ctx)
{
    box_pick_state_t *st = ctx;
    (void)world;
    ecs_vec_append_t(NULL, &st->emits, box_input_emit_t)[0] = *emit;
}

/** Check whether an InputBlocker element is on the path of what a cursor picks, so presses and
 * the wheel there do not reach the actions, the blocks callback of the router.
 *
 * @param world The world
 * @param cursor Cursor in window points
 * @param ctx The module state
 * @return Whether the pick at the cursor goes to or through an InputBlocker
 */
static bool box_pick_blocks(
    ecs_world_t *world,
    const vec2 cursor,
    void *ctx)
{
    box_pick_state_t *st = ctx;
    box_pick_t pick;
    ecs_entity_t e;
    int32_t depth = 0;
    bool result = false;
    box_pick_at(world, st, cursor, &pick);
    for (e = pick.target; e && !result && depth < BOX_PICK_CHAIN_MAX; depth ++) {
        result = ecs_has_id(world, e, InputBlocker);
        e = ecs_get_parent(world, e);
    }

    return result;
}

/** Collect the entities an event goes to: the target up to its handler, else the screen. The
 * walk ends at an entity that is no longer alive, which a script of the event may have deleted.
 *
 * @param world The world
 * @param st The module state
 * @param pick The pick of the event
 * @param chain Receives the entities in delivery order
 * @return Number of entities
 */
static int32_t box_pick_chain(
    const ecs_world_t *world,
    const box_pick_state_t *st,
    const box_pick_t *pick,
    ecs_entity_t *chain)
{
    int32_t count = 0;
    ecs_entity_t e = pick->target;
    if (!e) {
        chain[count ++] = st->screen;
    }

    while (e && ecs_is_alive(world, e) && count < BOX_PICK_CHAIN_MAX) {
        chain[count ++] = e;
        e = e == pick->handler ? 0 : ecs_get_parent(world, e);
    }

    return count;
}

/** Express the hit of a pick in the local space of an entity of its path.
 *
 * @param world The world
 * @param e The entity
 * @param pick The pick
 * @param evt The event, whose local position is written
 */
static void box_pick_local(
    const ecs_world_t *world,
    ecs_entity_t e,
    const box_pick_t *pick,
    EcsScriptMouseEvent *evt)
{
    const WorldTransform *t = pick->target ? ecs_get(world, e, WorldTransform) : NULL;
    evt->local_x = evt->screen_x;
    evt->local_y = evt->screen_y;
    if (t) {
        mat4 inv;
        vec3 local;
        glm_mat4_inv(ECS_CONST_CAST(vec4*, t->m), inv);
        glm_mat4_mulv3(inv, ECS_CONST_CAST(float*, pick->point), 1.0f, local);
        evt->local_x = local[0];
        evt->local_y = local[1];
    }
}

/** Move a tag from the entity that carries it to another one.
 *
 * @param world The world
 * @param tag The tag
 * @param from The entity that carries it, 0 for none
 * @param to The entity that gets it, 0 for none
 */
static void box_pick_tag(
    ecs_world_t *world,
    ecs_entity_t tag,
    ecs_entity_t from,
    ecs_entity_t to)
{
    if (from != to && from && ecs_is_alive(world, from)) {
        ecs_remove_id(world, from, tag);
    }

    if (from != to && to && ecs_is_alive(world, to)) {
        ecs_add_id(world, to, tag);
    }
}

/** Find the shape of the pointer cursor over a pick: the Cursor of the first entity on the path
 * from its target to its handler.
 *
 * @param world The world
 * @param st The module state
 * @param pick The pick
 * @return The shape, CursorDefault when no entity on the path has a Cursor
 */
static PointerCursor box_pick_shape(
    const ecs_world_t *world,
    const box_pick_state_t *st,
    const box_pick_t *pick)
{
    ecs_entity_t chain[BOX_PICK_CHAIN_MAX];
    int32_t i, count = pick->target ? box_pick_chain(world, st, pick, chain) : 0;
    PointerCursor result = CursorDefault;
    for (i = 0; i < count && ecs_is_alive(world, chain[i]); i ++) {
        const Cursor *c = ecs_get(world, chain[i], Cursor);
        if (c) {
            result = c->shape;
            break;
        }
    }

    return result;
}

/** Publish a pick in PickStatus, Hovered and Pressed.
 *
 * @param world The world
 * @param st The module state
 * @param pick The pick
 * @param cursor The cursor of the pick
 * @param press Whether a button went down with the event of the pick
 */
static void box_pick_publish(
    ecs_world_t *world,
    box_pick_state_t *st,
    const box_pick_t *pick,
    const vec2 cursor,
    bool press)
{
    ecs_entity_t pressed = press ? pick->target : (st->buttons ? st->pressed : 0);
    pressed = pressed && ecs_is_alive(world, pressed) ? pressed : 0;
    const PickStatus *cur = ecs_singleton_get(world, PickStatus);
    PickStatus status;
    ecs_os_zeromem(&status);
    status.target = pick->target;
    status.handler = pick->handler;
    status.view = pick->view;
    status.pressed = pressed;
    status.captured = st->capture.target;
    if (st->capture.target) {
        status.drag[0] = cursor[0] - st->capture.start[0];
        status.drag[1] = cursor[1] - st->capture.start[1];
    }

    status.hit = pick->hit;
    status.hit_view = pick->hit_view;
    status.distance = pick->hit_distance;
    status.cursor[0] = cursor[0];
    status.cursor[1] = cursor[1];
    status.shape = box_pick_shape(world, st, pick);
    glm_vec3_copy(ECS_CONST_CAST(float*, pick->hit_point), status.point);
    box_pick_tag(world, Hovered, st->hovered, pick->target);
    box_pick_tag(world, Pressed, st->pressed, pressed);
    st->hovered = pick->target;
    st->pressed = pressed;
    if (!cur || ecs_os_memcmp_t(cur, &status, PickStatus)) {
        ecs_singleton_set_ptr(world, PickStatus, &status);
    }
}

/** Start or end the capture of a button after an event was delivered.
 *
 * A press that lands on an Interactive entity while nothing is captured makes it capture the
 * pointer. The capture ends when the button that started it is released.
 *
 * @param st The module state
 * @param src The event
 * @param pick The pick the event was delivered by
 * @param pressed The buttons that went down with the event
 */
static void box_pick_capture(
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick,
    uint32_t pressed)
{
    box_pick_capture_t *cap = &st->capture;
    if (cap->target && !(src->buttons & cap->buttons)) {
        ecs_os_zeromem(cap);
    }

    if (!cap->target && pressed && pick->target && pick->handler) {
        cap->target = pick->target;
        cap->handler = pick->handler;
        cap->view = pick->view;
        cap->surface = pick->surface;
        cap->offset[0] = pick->surface_cursor[0] - src->screen_x;
        cap->offset[1] = pick->surface_cursor[1] - src->screen_y;
        cap->start[0] = src->screen_x;
        cap->start[1] = src->screen_y;
        cap->buttons = pressed;
        glm_vec3_copy(ECS_CONST_CAST(float*, pick->point), cap->point);
    }
}

/** Express the screen position of the event being delivered in the local space of a parent of
 * its target, the to_local hook of flecs.script.input.
 *
 * @param world The world
 * @param entity The parent
 * @param screen_x X of the cursor in window points
 * @param screen_y Y of the cursor in window points
 * @param local_x Receives the local X
 * @param local_y Receives the local Y
 * @param ctx The module state
 */
static void box_pick_to_local(
    ecs_world_t *world,
    ecs_entity_t entity,
    float screen_x,
    float screen_y,
    float *local_x,
    float *local_y,
    void *ctx)
{
    const box_pick_state_t *st = ctx;
    EcsScriptMouseEvent evt = { .screen_x = screen_x, .screen_y = screen_y };
    if (st->sending) {
        box_pick_local(world, entity, st->sending, &evt);
    }

    *local_x = st->sending ? evt.local_x : screen_x;
    *local_y = st->sending ? evt.local_y : screen_y;
}

/** Decide which entity gets the flecs focus through the focus callback of the hook, the
 * to_focus hook of flecs.script.input. Without one the focus goes where flecs asks.
 *
 * box.picking is the only module that sets the hooks of flecs.script.input; box.ui decides the
 * focus through box_pick_hook_t, so the two do not replace each other's hooks.
 *
 * @param world The world
 * @param entity The entity flecs would focus, 0 for none
 * @param current The entity with the focus, 0 for none
 * @param source What moves the focus
 * @param ctx The module state
 * @return The entity that gets the focus
 */
static ecs_entity_t box_pick_to_focus(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t current,
    ecs_script_focus_source_t source,
    void *ctx)
{
    box_pick_state_t *st = ctx;
    const box_pick_t *pick = source == EcsScriptFocusPress ? st->sending : NULL;

    return st->hook.focus ?
        st->hook.focus(world, st, entity, current, source, pick, st->hook.ctx) : entity;
}

/** Get the id flecs gave the pointer event box.picking just sent, which is its last mouse event
 * unless a script sent another one while it was delivered.
 *
 * @param world The world
 * @return The id, 0 when flecs has no mouse event
 */
static uint64_t box_pick_sent_id(
    const ecs_world_t *world)
{
    EcsScriptMouseEvent last;
    ecs_script_get_mouse_state(world, &last, NULL);

    return last.id;
}

/** Move the cursor of flecs.script.input onto a press before it is sent, when the last event
 * picking delivered was at another target or local point.
 *
 * flecs.script.input computes the delta of an event with a zero delta from the local point of
 * the previous event, and the press fires on.drag. A press must start a drag with a zero delta,
 * so the previous event has to be a move of the same target at the same local point.
 *
 * @param world The world
 * @param st The module state
 * @param press The press, aimed at its target in its local space
 */
static void box_pick_align(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *press)
{
    EcsScriptMouseEvent move = *press;
    bool aligned = st->has_event && st->event.target == press->target &&
        st->event.local_x == press->local_x && st->event.local_y == press->local_y;
    if (st->buttons || aligned) {
        goto done;
    }

    move.button = 0;
    move.buttons = st->buttons;
    move.delta_x = move.delta_y = 0;
    if (st->has_cursor) {
        move.delta_x = press->screen_x - st->cursor[0];
        move.delta_y = press->screen_y - st->cursor[1];
    }

    ecs_script_mouse_event(world, &move);
done:
    return;
}

/** Send a pointer event along the path of its pick through flecs.script.input.
 *
 * @param world The world
 * @param st The module state
 * @param src The event, aimed at the screen
 * @param pick The pick at the cursor of the event
 */
static void box_pick_send(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick)
{
    EcsScriptMouseEvent evt = *src;
    uint32_t pressed = src->buttons & ~st->buttons;
    uint32_t released = st->buttons & ~src->buttons;
    bool press = pressed != 0;
    if (st->has_cursor && !press) {
        evt.delta_x = src->screen_x - st->cursor[0];
        evt.delta_y = src->screen_y - st->cursor[1];
    } else {
        evt.delta_x = evt.delta_y = 0;
    }

    evt.target = pick->target ? pick->target : st->screen;
    box_pick_local(world, evt.target, pick, &evt);
    st->sending = pick;
    if (press) {
        box_pick_align(world, st, &evt);
    }

    st->event = evt;
    st->event.id = 0;
    st->has_event = true;
    ecs_script_mouse_event(world, &evt);
    st->sending = NULL;
    st->event.id = box_pick_sent_id(world);

    box_pick_triggers(world, st, src, pick, pressed, released);
    if (st->hook.pointer) {
        st->hook.pointer(world, st, src, pick, pressed, released, st->hook.ctx);
    }

    st->cursor[0] = src->screen_x;
    st->cursor[1] = src->screen_y;
    st->buttons = src->buttons;
    st->has_cursor = true;
    st->last = pick->target;
    box_pick_capture(st, src, pick, pressed);
    box_pick_publish(world, st, pick, st->cursor, press);
}

/** Resolve the on.scroll waiters of the innermost entity on the path of the pick of a wheel
 * movement that waits for one. The path goes past the handler of the pick, so an Interactive
 * element inside a scroll view, such as a button, does not keep the wheel from the view.
 *
 * @param world The world
 * @param st The module state
 * @param src The wheel event, its delta is the wheel offset
 * @param pick The pick at the cursor of the event
 */
static void box_pick_scroll(
    ecs_world_t *world,
    box_pick_state_t *st,
    const EcsScriptMouseEvent *src,
    const box_pick_t *pick)
{
    ecs_entity_t e = pick->target ? pick->target : st->screen;
    int32_t w, resolved = 0;
    for (; e && !resolved && ecs_is_alive(world, e);
        e = pick->target ? ecs_get_parent(world, e) : 0)
    {
        EcsScriptMouseEvent evt = *src;
        evt.target = e;
        box_pick_local(world, e, pick, &evt);
        for (w = 0; w < ecs_vec_count(&st->waiters); ) {
            box_pick_waiter_t waiter = *ecs_vec_get_t(&st->waiters, box_pick_waiter_t, w);
            if (waiter.target != e) {
                w ++;
                continue;
            }

            ecs_vec_remove_ordered_t(&st->waiters, box_pick_waiter_t, w);
            ecs_script_future_resolve(waiter.future, &(ecs_value_t){
                .type = ecs_id(EcsScriptMouseEvent), .ptr = &evt });
            ecs_script_future_release(waiter.future);
            resolved ++;
        }
    }

    if (resolved) {
        ecs_script_tasks_progress(world);
    }
}

/** Check whether either key of a modifier is held.
 *
 * @param in The Input singleton
 * @param left The left key
 * @param right The right key
 * @return Whether one of them is held
 */
static bool box_pick_modifier(
    const Input *in,
    Key left,
    Key right)
{
    return in && (in->keys[left].down || in->keys[right].down);
}

/** Pick again under a cursor that did not move, and send a move when the target changed.
 *
 * @param world The world
 * @param st The module state
 */
static void box_pick_refresh(
    ecs_world_t *world,
    box_pick_state_t *st)
{
    const Input *in = ecs_singleton_get(world, Input);
    box_pick_t pick;
    box_pick_resolve(world, st, st->cursor, &pick);
    if (pick.target == st->last) {
        box_pick_publish(world, st, &pick, st->cursor, false);
    } else {
        EcsScriptMouseEvent evt = {
            .screen_x = st->cursor[0],
            .screen_y = st->cursor[1],
            .buttons = st->buttons,
            .ctrl = box_pick_modifier(in, KeyCtrlLeft, KeyCtrlRight),
            .shift = box_pick_modifier(in, KeyShiftLeft, KeyShiftRight),
            .alt = box_pick_modifier(in, KeyAltLeft, KeyAltRight),
            .meta = box_pick_modifier(in, KeySuperLeft, KeySuperRight)
        };
        box_pick_send(world, st, &evt, &pick);
    }
}

/** Check whether a pointer event only moves the cursor and the next event of the frame moves it
 * again with the same buttons and modifiers, so only the later one needs a pick.
 *
 * Moves are coalesced once per frame like the animation frame aligned pointer moves of
 * browsers; presses, releases, the wheel and keyboard events are never skipped or reordered.
 *
 * @param st The module state
 * @param i Index of the event in the queue of the frame
 * @param buttons Buttons held before the event
 * @return Whether the event can be skipped
 */
static bool box_pick_coalesced(
    const box_pick_state_t *st,
    int32_t i,
    uint32_t buttons)
{
    const box_input_emit_t *emits = ecs_vec_first_t(&st->emits, box_input_emit_t);
    const EcsScriptMouseEvent *a = &emits[i].mouse;
    const EcsScriptMouseEvent *b = &emits[i + 1].mouse;
    bool result = false;
    if (i + 1 < ecs_vec_count(&st->emits) && emits[i].is_mouse && !emits[i].is_scroll &&
        emits[i + 1].is_mouse && !emits[i + 1].is_scroll)
    {
        result = a->buttons == buttons && b->buttons == buttons && a->ctrl == b->ctrl &&
            a->shift == b->shift && a->alt == b->alt && a->meta == b->meta;
    }

    return result;
}

void BoxPickingUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = ECS_CONST_CAST(ecs_world_t*, ecs_get_world(it->world));
    box_pick_state_t *st = it->ctx;
    bool pointer = false;
    int32_t i;
    box_pick_meshes_prune(world, st);
    box_pick_triggers_check(world, st);
    if (st->hook.begin) {
        st->hook.begin(world, st, st->hook.ctx);
    }

    for (i = 0; i < ecs_vec_count(&st->emits); i ++) {
        box_input_emit_t emit = *ecs_vec_get_t(&st->emits, box_input_emit_t, i);
        box_pick_t pick;
        if (!emit.is_mouse) {
            if (st->hook.key) {
                st->hook.key(world, st, &emit, st->hook.ctx);
            } else {
                ecs_script_keyboard_event(world, &emit.keyboard);
            }

            continue;
        }

        pointer = true;
        if (box_pick_coalesced(st, i, st->buttons)) {
            continue;
        }

        if (emit.is_scroll) {
            box_pick_at(world, st, (vec2){ emit.mouse.screen_x, emit.mouse.screen_y }, &pick);
            box_pick_scroll(world, st, &emit.mouse, &pick);
        } else {
            box_pick_resolve(
                world, st, (vec2){ emit.mouse.screen_x, emit.mouse.screen_y }, &pick);
            box_pick_send(world, st, &emit.mouse, &pick);
        }
    }

    ecs_vec_clear(&st->emits);
    if (!pointer && st->has_cursor) {
        box_pick_refresh(world, st);
    }

    box_input_cursor_shape(world, ecs_singleton_get(world, PickStatus)->shape);
}

void box_pick_hook_set(
    ecs_world_t *world,
    const box_pick_hook_t *hook)
{
    const PickingState *ps = ecs_singleton_get(world, PickingState);
    if (ps && ps->state) {
        ecs_os_zeromem(&ps->state->hook);
        if (hook) {
            ps->state->hook = *hook;
        }
    }
}

bool box_view_cursor_ray(
    const ecs_world_t *world,
    ecs_entity_t view,
    vec3 origin,
    vec3 dir)
{
    const PickingState *ps = ecs_id(PickingState) ?
        ecs_singleton_get(world, PickingState) : NULL;

    return ps && ps->state && box_pick_cursor_ray(world, ps->state, view, origin, dir);
}

/** Suspend a task until the wheel moves over the entity of the argument.
 *
 * @param ctx Call context, whose ctx is the module state
 * @param argc Number of arguments
 * @param argv The entity
 * @param future Future the wheel movement resolves
 */
static void box_pick_scroll_wait(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    box_pick_state_t *st = ctx->ctx;
    ecs_entity_t target = *(ecs_entity_t*)argv[0].ptr;
    (void)argc;
    if (!target) {
        ecs_script_future_reject(future, "on.scroll expects an entity");
        ecs_script_future_release(future);
    } else {
        ecs_vec_append_t(NULL, &st->waiters, box_pick_waiter_t)[0] =
            (box_pick_waiter_t){ .target = target, .future = future };
    }
}

/** Drop the waiter of a cancelled task.
 *
 * @param ctx Call context, whose ctx is the module state
 * @param future The cancelled future
 */
static void box_pick_scroll_cancel(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    box_pick_state_t *st = ctx->ctx;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&st->waiters); i ++) {
        if (ecs_vec_get_t(&st->waiters, box_pick_waiter_t, i)->future == future) {
            ecs_script_future_release(future);
            ecs_vec_remove_ordered_t(&st->waiters, box_pick_waiter_t, i);
            break;
        }
    }
}

void BoxPickingImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxPicking);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxLayout);
    ECS_IMPORT(world, BoxText);
    ECS_IMPORT(world, BoxInput);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxSpatial);

    ECS_TAG_DEFINE(world, Interactive);
    ECS_TAG_DEFINE(world, InputBlocker);
    ECS_TAG_DEFINE(world, Hovered);
    ECS_TAG_DEFINE(world, Pressed);
    ECS_TAG_DEFINE(world, PickThrough);
    ECS_META_COMPONENT(world, ActionTriggerMode);
    ECS_META_COMPONENT(world, ActionTrigger);
    ECS_META_COMPONENT(world, Cursor);
    ECS_META_COMPONENT(world, PickStatus);
    ecs_set_name_prefix(world, "Picking");
    ECS_COMPONENT_DEFINE(world, PickingState);
    ecs_set_name_prefix(world, NULL);
    ecs_set_hooks(world, PickingState, {
        .ctor = flecs_default_ctor,
        .dtor = box_pick_state_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    box_pick_state_t *st = ecs_os_calloc_t(box_pick_state_t);
    st->screen = box_input_screen(world);
    ecs_vec_init_t(NULL, &st->emits, box_input_emit_t, 0);
    ecs_vec_init_t(NULL, &st->candidates, box_pick_candidate_t, 0);
    ecs_vec_init_t(NULL, &st->waiters, box_pick_waiter_t, 0);
    ecs_vec_init_t(NULL, &st->roots, ecs_entity_t, 0);
    ecs_map_init(&st->meshes, NULL);
    box_pick_queries_init(world, st);
    box_pick_triggers_import(world, st);
    ecs_singleton_ensure(world, PickingState)->state = st;
    ecs_singleton_set(world, PickStatus, {0});
    box_input_router_set(world, &(box_input_router_t){
        .route = box_pick_route, .blocks = box_pick_blocks, .ctx = st });
    ecs_script_set_event_hooks(world, &(ecs_script_event_hooks_t){
        .to_local = box_pick_to_local, .to_focus = box_pick_to_focus, .ctx = st });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .phase = EcsPreUpdate,
        .run = BoxPickingUpdate,
        .ctx = st,
        .immediate = true
    });

    ecs_async_function(world, {
        .name = "scroll",
        .parent = ecs_lookup(world, "flecs.script.on"),
        .return_type = ecs_id(EcsScriptMouseEvent),
        .params = {{ "target", ecs_id(ecs_entity_t) }},
        .callback = box_pick_scroll_wait,
        .cancel = box_pick_scroll_cancel,
        .ctx = st
    });
}
