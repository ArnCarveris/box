/**
 * @file picking/picking.c
 * @brief Registers box.picking and sends the pointer events of a frame to what the cursor picks.
 *
 * box.input hands every event of a frame to the router of this module, which sends them in
 * order in PreUpdate, picking what the last frame placed, so handlers act before the layout.
 */

#define BOX_PICKING_IMPL
#include "picking.h"

ECS_COMPONENT_DECLARE(PickingState);
ECS_TAG_DECLARE(Interactive);
ECS_TAG_DECLARE(InputBlocker);
ECS_TAG_DECLARE(Hovered);
ECS_TAG_DECLARE(Pressed);

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
    st->memo = (box_pick_memo_t){
        .pick = pick, .cursor = { cursor[0], cursor[1] },
        .frame = ecs_get_world_info(world)->frame_count_total, .valid = true };
    for (e = pick.target; e && !result && depth < BOX_PICK_CHAIN_MAX; depth ++) {
        result = ecs_has_id(world, e, InputBlocker);
        e = ecs_get_parent(world, e);
    }

    return result;
}

/** Collect the entities an event goes to: the target up to its handler, else the screen.
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

    while (e && count < BOX_PICK_CHAIN_MAX) {
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

/** Send a pointer event along the path of its pick through flecs.script.event.
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
    ecs_entity_t chain[BOX_PICK_CHAIN_MAX];
    int32_t i, count = box_pick_chain(world, st, pick, chain);
    EcsScriptMouseEvent evt = *src;
    uint32_t pressed = src->buttons & ~st->buttons;
    bool press = pressed != 0;
    if (st->has_cursor && !press) {
        evt.delta_x = src->screen_x - st->cursor[0];
        evt.delta_y = src->screen_y - st->cursor[1];
    } else {
        evt.delta_x = evt.delta_y = 0;
    }

    for (i = 0; i < count && ecs_is_alive(world, chain[i]); i ++) {
        evt.target = chain[i];
        box_pick_local(world, chain[i], pick, &evt);
        ecs_script_mouse_event(world, &evt);
    }

    st->memo.valid = false;
    st->cursor[0] = src->screen_x;
    st->cursor[1] = src->screen_y;
    st->buttons = src->buttons;
    st->has_cursor = true;
    st->last = pick->target;
    box_pick_capture(st, src, pick, pressed);
    box_pick_publish(world, st, pick, st->cursor, press);
}

/** Resolve the on.scroll waiters along the path of the pick of a wheel movement.
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
    ecs_entity_t chain[BOX_PICK_CHAIN_MAX];
    int32_t i, w, resolved = 0, count = box_pick_chain(world, st, pick, chain);
    for (i = 0; i < count && ecs_is_alive(world, chain[i]); i ++) {
        EcsScriptMouseEvent evt = *src;
        evt.target = chain[i];
        box_pick_local(world, chain[i], pick, &evt);
        for (w = 0; w < ecs_vec_count(&st->waiters); ) {
            box_pick_waiter_t waiter = *ecs_vec_get_t(&st->waiters, box_pick_waiter_t, w);
            if (waiter.target != chain[i]) {
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
        st->memo.valid = false;
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
    for (i = 0; i < ecs_vec_count(&st->emits); i ++) {
        box_input_emit_t emit = *ecs_vec_get_t(&st->emits, box_input_emit_t, i);
        box_pick_t pick;
        if (!emit.is_mouse) {
            st->memo.valid = false;
            ecs_script_keyboard_event(world, &emit.keyboard);
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
    st->memo.valid = false;
    if (!pointer && st->has_cursor) {
        box_pick_refresh(world, st);
    }
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
    ECS_META_COMPONENT(world, PickStatus);
    ECS_COMPONENT_DEFINE(world, PickingState);
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
    ecs_map_init(&st->meshes, NULL);
    box_pick_queries_init(world, st);
    ecs_singleton_ensure(world, PickingState)->state = st;
    ecs_singleton_set(world, PickStatus, {0});
    box_input_router_set(world, &(box_input_router_t){
        .route = box_pick_route, .blocks = box_pick_blocks, .ctx = st });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "PickingUpdate" }),
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
