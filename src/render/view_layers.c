/**
 * @file render/view_layers.c
 * @brief Render layer registry: the bit of every layer and the masks it builds.
 *
 * A layer gets its bit the first time an entity or a light joins it, so a scene
 * that uses no layer keeps an empty registry and pays nothing for them.
 */

#include "render_internal.h"

/** Index of a layer entity in the registry.
 *
 * @param state The registry
 * @param layer The layer entity
 * @return The index, -1 when the layer has no bit
 */
static int32_t box_view_layer_index(
    const RenderState *state,
    ecs_entity_t layer)
{
    int32_t result = -1;
    const ecs_entity_t *entities = ecs_vec_first_t(&state->view_layers, ecs_entity_t);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->view_layers) && result < 0; i ++) {
        if (entities[i] == layer) {
            result = i;
        }
    }

    return result;
}

/** Bit of a layer entity that already has one.
 *
 * @param state The registry
 * @param layer The layer entity
 * @return The bit, 0 when the layer has none
 */
static uint32_t box_view_layer_lookup(
    const RenderState *state,
    ecs_entity_t layer)
{
    int32_t index = box_view_layer_index(state, layer);

    return index < 0 ? 0u : 1u << index;
}

uint32_t box_render_layer_bit(
    RenderState *state,
    ecs_entity_t layer)
{
    uint32_t result = box_view_layer_lookup(state, layer);
    if (result) {
        goto done;
    }

    if (ecs_vec_count(&state->view_layers) >= BOX_VIEW_LAYER_BITS) {
        if (!state->layer_full) {
            ecs_warn("box.view: more than %d layers are used, the rest is shown everywhere",
                BOX_VIEW_LAYER_BITS);
            state->layer_full = true;
        }

        goto done;
    }

    result = 1u << ecs_vec_count(&state->view_layers);
    ecs_vec_append_t(NULL, &state->view_layers, ecs_entity_t)[0] = layer;
done:
    return result;
}

uint32_t box_render_entity_layers(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    uint32_t result = 0;
    ecs_entity_t cur = e;
    while (cur) {
        int32_t index = 0;
        ecs_entity_t target;
        while ((target = ecs_get_target(world, cur, Layer, index))) {
            result |= box_view_layer_lookup(state, target);
            index ++;
        }

        cur = ecs_get_target(world, cur, EcsIsA, 0);
    }

    return result;
}

uint32_t box_render_view_layers(
    const RenderState *state,
    const ecs_entity_t *list)
{
    uint32_t result = 0;
    int32_t i;
    for (i = 0; i < BOX_VIEW_LAYER_MAX && list[i]; i ++) {
        result |= box_view_layer_lookup(state, list[i]);
    }

    return i ? result : BOX_VIEW_LAYER_ALL;
}

/** An entity or a light joined or left a layer, so its instances resolve a new mask.
 *
 * A prefab hands its layers to every instance, which the pair does not report,
 * so a change on a prefab requeues every instance of the world instead.
 *
 * @param it Iterator over the entities the pair changed on
 */
static void box_view_layer_on_change(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (!state) {
        goto done;
    }

    state->layer_used = true;
    if (it->event == EcsOnAdd) {
        box_render_layer_bit(state, ecs_pair_second(it->world, it->event_id));
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (ecs_has_id(it->world, it->entities[i], EcsPrefab)) {
            box_render_mark_all(it->world, state);
        } else {
            box_render_mark_subtree(it->world, it->entities[i]);
        }
    }

done:
    return;
}

void box_render_view_layers_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(Layer, EcsWildcard) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_view_layer_on_change
    });
}
