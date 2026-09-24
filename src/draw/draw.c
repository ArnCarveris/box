/**
 * @file draw/draw.c
 * @brief The box.draw module: the instance pool behind box_draw and the overlay view.
 *
 * A draw call looks its instance up by asset and key, creates it under
 * box.draw.instances when it is new, moves it and stamps it with the frame.
 *
 * A PostUpdate system hides every instance that was not drawn this frame and
 * deletes the ones that stayed hidden for BOX_DRAW_RETIRE_FRAMES frames.
 */

#define BOX_DRAW_IMPL
#include "draw_internal.h"
#include "../render/render_internal.h"

ECS_COMPONENT_DECLARE(DrawState);

/** Key of the instance map for an asset and a key.
 *
 * @param asset The asset
 * @param key The key of the draw call
 * @return The map key
 */
static ecs_map_key_t box_draw_map_key(
    ecs_entity_t asset,
    uint64_t key)
{
    return (asset * 0x9E3779B97F4A7C15ull) ^ (key * 0xC2B2AE3D27D4EB4Full);
}

/** Test whether an entity is a script template.
 *
 * @param world The world
 * @param e The entity
 * @return true when the entity is instantiated by adding it as a component
 */
static bool box_draw_is_template(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const EcsScript *script = ecs_get(world, e, EcsScript);

    return script != NULL && script->template_ != NULL;
}

/** Create the view that draws an overlay layer over the scene.
 *
 * The layer is registered as a UI layer here, on the first draw into it, so a
 * world that never draws an overlay keeps its layer registry empty.
 *
 * @param world The world
 * @param layer The overlay layer the view draws
 * @param name Name of the view under box.draw
 * @param priority Order in the frame the view renders at
 * @return The view entity
 */
static ecs_entity_t box_draw_view_create(
    ecs_world_t *world,
    ecs_entity_t layer,
    const char *name,
    int32_t priority)
{
    box_render_add_ui_layer(world, layer);
    ecs_entity_t module = ecs_lookup(world, "box.draw");
    ecs_entity_t view = ecs_entity(world, { .name = name, .parent = module });
    ecs_set(world, view, View, {
        .camera = 0,
        .layers = { layer },
        .priority = priority,
        .strict_layers = true,
        .shadows = { .enabled = false },
        .culling = { .enabled = false },
        .enabled = true
    });

    return view;
}

/** Create the view of the overlay layer a draw goes to when it does not exist yet.
 *
 * @param world The world
 * @param state The module state
 * @param on_top Whether the draw goes to the layer on top
 */
static void box_draw_overlay_view(
    ecs_world_t *world,
    DrawState *state,
    bool on_top)
{
    if (on_top && (!state->top_view || !ecs_is_alive(world, state->top_view))) {
        ecs_entity_t view = box_draw_view_create(
            world, state->top, "top_view", BOX_DRAW_TOP_PRIORITY);
        state = box_draw_state(world);
        state->top_view = view;
    } else if (!on_top && (!state->view || !ecs_is_alive(world, state->view))) {
        ecs_entity_t view = box_draw_view_create(
            world, state->overlay, "overlay_view", BOX_DRAW_OVERLAY_PRIORITY);
        state = box_draw_state(world);
        state->view = view;
    }
}

/** Point an overlay view at a camera and an occluder.
 *
 * @param world The world
 * @param e The view, 0 when it does not exist yet
 * @param camera The camera
 * @param occluder The view whose depth hides this one, 0 for none
 */
static void box_draw_view_follow(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t camera,
    ecs_entity_t occluder)
{
    if (!e || !ecs_is_alive(world, e)) {
        goto done;
    }

    View *view = ecs_get_mut(world, e, View);
    if (view->camera != camera || view->occluder != occluder) {
        view->camera = camera;
        view->occluder = occluder;
        ecs_modified(world, e, View);
    }

done:
    return;
}

/** Point the overlay views at the camera of the primary view of the scene.
 *
 * The primary view is also the occluder of the overlay, so an overlay draw
 * behind the scene shows faded instead of over it. The view on top has none.
 *
 * @param world The world
 * @param state The module state
 */
static void box_draw_follow_camera(
    ecs_world_t *world,
    DrawState *state)
{
    bool overlay = state->view && ecs_is_alive(world, state->view);
    bool top = state->top_view && ecs_is_alive(world, state->top_view);
    if (!overlay && !top) {
        goto done;
    }

    RenderState *render = box_render_state(world);
    ecs_entity_t primary = render ? box_render_primary_view(world, render) : 0;
    const View *scene = primary && primary != state->view && primary != state->top_view ?
        ecs_get(world, primary, View) : NULL;
    ecs_entity_t camera = scene ? scene->camera : 0;
    ecs_entity_t occluder = scene ? primary : 0;
    if (!camera) {
        camera = box_render_active_camera(world);
    }

    ecs_entity_t top_view = state->top_view;
    box_draw_view_follow(world, state->view, camera, occluder);
    box_draw_view_follow(world, top_view, camera, 0);
done:
    return;
}

/** Put every descendant of an instance in the overlay layer.
 *
 * @param world The world
 * @param e The instance
 * @param layer The overlay layer
 */
static void box_draw_layer_subtree(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t layer)
{
    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &children, ecs_entity_t)[0] = it.entities[i];
        }
    }

    int32_t i, count = ecs_vec_count(&children);
    for (i = 0; i < count; i ++) {
        ecs_entity_t child = ecs_vec_get_t(&children, ecs_entity_t, i)[0];
        if (!ecs_has_pair(world, child, Layer, layer)) {
            ecs_add_pair(world, child, Layer, layer);
        }

        box_draw_layer_subtree(world, child, layer);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

/** Create the instance of a draw call.
 *
 * @param world The world
 * @param state The module state
 * @param desc The draw call
 * @param prefab Whether the asset is a prefab rather than a template
 * @return The instance entity
 */
static ecs_entity_t box_draw_create(
    ecs_world_t *world,
    DrawState *state,
    const box_draw_desc_t *desc,
    bool prefab)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, state->scope);
    ecs_add_id(world, e, DynamicTransform);
    if (prefab) {
        ecs_add_pair(world, e, EcsIsA, desc->asset);
    } else {
        ecs_add_id(world, e, desc->asset);
        if (ecs_has_pair(world, e, EcsScriptTemplatePending, desc->asset)) {
            ecs_script_template_update(world, e, desc->asset);
        }
    }

    if (desc->overlay) {
        ecs_add_pair(world, e, Layer, desc->on_top ? state->top : state->overlay);
        box_draw_overlay_view(world, state, desc->on_top);
    }

    return e;
}

ecs_entity_t box_draw(
    ecs_world_t *world,
    const box_draw_desc_t *desc)
{
    DrawState *state = box_draw_state(world);
    ecs_entity_t result = 0;
    ecs_assert(state != NULL, ECS_INVALID_OPERATION, "import box.draw first");
    bool prefab = ecs_has_id(world, desc->asset, EcsPrefab);
    if (!prefab && !box_draw_is_template(world, desc->asset)) {
        goto done;
    }

    int64_t frame = ecs_get_world_info(world)->frame_count_total;
    if (state->frame != frame) {
        state->frame = frame;
        state->calls = 0;
    }

    uint64_t key = desc->key ? desc->key : (uint64_t)(++ state->calls);
    ecs_map_key_t map_key = box_draw_map_key(desc->asset, key);
    ecs_map_val_t *slot = ecs_map_get(&state->instances, map_key);
    const DrawInstance *stored = slot && ecs_is_alive(world, (ecs_entity_t)slot[0]) ?
        ecs_get(world, (ecs_entity_t)slot[0], DrawInstance) : NULL;
    if (stored && stored->asset == desc->asset && stored->key == key) {
        result = (ecs_entity_t)slot[0];
    } else {
        result = box_draw_create(world, state, desc, prefab);
        state = box_draw_state(world);
        ecs_map_ensure(&state->instances, map_key)[0] = result;
    }

    ecs_set(world, result, DrawInstance, {
        .asset = desc->asset, .key = key, .frame = frame, .overlay = desc->overlay,
        .on_top = desc->overlay && desc->on_top });
    ecs_set(world, result, Position, { desc->position.x, desc->position.y, desc->position.z });
    ecs_set(world, result, Rotation, { desc->rotation.x, desc->rotation.y, desc->rotation.z });
    bool unit = desc->scale.x == 0 && desc->scale.y == 0 && desc->scale.z == 0;
    ecs_set(world, result, Scale, {
        unit ? 1 : desc->scale.x, unit ? 1 : desc->scale.y, unit ? 1 : desc->scale.z });
    if (ecs_has_id(world, result, Hidden)) {
        ecs_remove_id(world, result, Hidden);
    }

    if (desc->overlay) {
        box_draw_layer_subtree(world, result, desc->on_top ? state->top : state->overlay);
    }

done:
    return result;
}

ecs_entity_t box_draw_overlay_layer(
    ecs_world_t *world)
{
    DrawState *state = box_draw_state(world);

    return state ? state->overlay : 0;
}

ecs_entity_t box_draw_top_layer(
    ecs_world_t *world)
{
    DrawState *state = box_draw_state(world);

    return state ? state->top : 0;
}

/** Hide the instances that were not drawn this frame and delete the stale ones.
 *
 * @param it Iterator over the instances of the pool
 */
static void BoxDrawRetire(
    ecs_iter_t *it)
{
    const DrawInstance *inst = ecs_field(it, DrawInstance, 0);
    int64_t frame = ecs_get_world_info(it->world)->frame_count_total;
    DrawState *state = box_draw_state(it->world);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (inst[i].frame == frame) {
            continue;
        }

        if (frame - inst[i].frame > BOX_DRAW_RETIRE_FRAMES) {
            ecs_map_remove(&state->instances,
                box_draw_map_key(inst[i].asset, inst[i].key));
            ecs_delete(it->stage, e);
        } else if (!ecs_has_id(it->world, e, Hidden)) {
            ecs_add_id(it->stage, e, Hidden);
        }
    }

    box_draw_follow_camera(it->world, state);
}

/** DrawState destructor: release the instance map.
 *
 * @param ptr The component
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_state_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    DrawState *state = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_map_fini(&state[i].instances);
    }
}

DrawState* box_draw_state(
    ecs_world_t *world)
{
    DrawState *result = NULL;
    if (ecs_id(DrawState) && !ecs_is_fini(world)) {
        result = ecs_get_mut(world, ecs_id(DrawState), DrawState);
    }

    return result;
}

void BoxDrawImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxDraw);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxRender);

    ECS_META_COMPONENT(world, DrawInstance);
    ECS_COMPONENT_DEFINE(world, DrawState);

    ecs_set_hooks(world, DrawInstance, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, DrawState, {
        .ctor = flecs_default_ctor,
        .dtor = box_draw_state_dtor,
        .flags = ECS_TYPE_HOOK_COPY_ILLEGAL | ECS_TYPE_HOOK_MOVE_ILLEGAL
    });

    ecs_add_pair(world, ecs_id(DrawInstance), EcsOnInstantiate, EcsDontInherit);

    ecs_entity_t module = ecs_set_scope(world, 0);
    ecs_set_scope(world, module);
    ecs_entity_t scope = ecs_entity(world, { .name = "instances" });
    ecs_entity_t overlay = ecs_entity(world, { .name = "overlay" });
    ecs_entity_t top = ecs_entity(world, { .name = "top" });

    DrawState *state = ecs_singleton_ensure(world, DrawState);
    ecs_map_init(&state->instances, NULL);
    state->scope = scope;
    state->overlay = overlay;
    state->top = top;
    state->frame = -1;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxDrawRetire" }),
        .phase = EcsPostUpdate,
        .query.terms = {{ .id = ecs_id(DrawInstance), .inout = EcsIn }},
        .callback = BoxDrawRetire
    });
}
