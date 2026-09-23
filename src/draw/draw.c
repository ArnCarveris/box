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

/** Create the view that draws the overlay layer over the scene.
 *
 * @param world The world
 * @param state The module state
 */
static void box_draw_overlay_view(
    ecs_world_t *world,
    DrawState *state)
{
    if (state->view && ecs_is_alive(world, state->view)) {
        goto done;
    }

    ecs_entity_t module = ecs_lookup(world, "box.draw");
    ecs_entity_t view = ecs_entity(world, {
        .name = "overlay_view", .parent = module
    });

    ecs_set(world, view, View, {
        .camera = 0,
        .layers = { state->overlay },
        .priority = BOX_DRAW_OVERLAY_PRIORITY,
        .strict_layers = true,
        .shadows = { .enabled = false },
        .culling = { .enabled = false },
        .enabled = true
    });

    state->view = view;
done:
    return;
}

/** Point the overlay view at the camera of the primary view of the scene.
 *
 * @param world The world
 * @param state The module state
 */
static void box_draw_follow_camera(
    ecs_world_t *world,
    DrawState *state)
{
    if (!state->view || !ecs_is_alive(world, state->view)) {
        goto done;
    }

    RenderState *render = box_render_state(world);
    ecs_entity_t primary = render ? box_render_primary_view(world, render) : 0;
    const View *scene = primary && primary != state->view ?
        ecs_get(world, primary, View) : NULL;
    ecs_entity_t camera = scene ? scene->camera : 0;
    if (!camera) {
        camera = box_render_active_camera(world);
    }

    View *view = ecs_get_mut(world, state->view, View);
    if (view->camera != camera) {
        view->camera = camera;
        ecs_modified(world, state->view, View);
    }

done:
    return;
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
        ecs_add_pair(world, e, Layer, state->overlay);
        box_draw_overlay_view(world, state);
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
        .asset = desc->asset, .key = key, .frame = frame, .overlay = desc->overlay });
    ecs_set(world, result, Position, { desc->position.x, desc->position.y, desc->position.z });
    ecs_set(world, result, Rotation, { desc->rotation.x, desc->rotation.y, desc->rotation.z });
    bool unit = desc->scale.x == 0 && desc->scale.y == 0 && desc->scale.z == 0;
    ecs_set(world, result, Scale, {
        unit ? 1 : desc->scale.x, unit ? 1 : desc->scale.y, unit ? 1 : desc->scale.z });
    if (ecs_has_id(world, result, Hidden)) {
        ecs_remove_id(world, result, Hidden);
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
    box_render_add_ui_layer(world, overlay);

    DrawState *state = ecs_singleton_ensure(world, DrawState);
    ecs_map_init(&state->instances, NULL);
    state->scope = scope;
    state->overlay = overlay;
    state->frame = -1;

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxDrawRetire" }),
        .phase = EcsPostUpdate,
        .query.terms = {{ .id = ecs_id(DrawInstance), .inout = EcsIn }},
        .callback = BoxDrawRetire
    });
}
