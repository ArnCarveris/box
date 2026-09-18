/**
 * @file render/extract_layers.c
 * @brief Material layer registry: the ordered layer list and the per material bits.
 *
 * The list only travels with a packet when it changed, and a registry flag keeps
 * a scene whose materials enable no layer out of the extraction entirely.
 */

#include "render_internal.h"
#include "../shader/shader_internal.h"

/** Order two registrations by their order and then by their entity.
 *
 * @param a The first registration
 * @param b The second registration
 * @return Negative when a runs first, positive when b does, zero when equal
 */
static int box_layer_compare(
    const void *a,
    const void *b)
{
    const box_layer_t *l = a;
    const box_layer_t *r = b;
    int result = (l->order > r->order) - (l->order < r->order);
    if (!result) {
        result = (l->entity > r->entity) - (l->entity < r->entity);
    }

    return result;
}

/** Rebuild the ordered registration list from the layer entities of the world.
 *
 * @param world The world
 * @param state The registry
 */
static void box_layer_rebuild(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_vec_clear(&state->layers);
    ecs_iter_t it = ecs_query_iter(world, state->layer_query);
    while (ecs_query_next(&it)) {
        MaterialLayer *layer = ecs_field(&it, MaterialLayer, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_layer_t *entry = ecs_vec_append_t(NULL, &state->layers, box_layer_t);
            entry->entity = it.entities[i];
            entry->order = layer[i].order;
        }
    }

    int32_t count = ecs_vec_count(&state->layers);
    qsort(ecs_vec_first_t(&state->layers, box_layer_t), (size_t)count, sizeof(box_layer_t),
        box_layer_compare);

    if (count > BOX_LAYER_MAX) {
        if (!state->layers_reported) {
            ecs_err("box.material: %d layers registered, only %d are applied",
                count, BOX_LAYER_MAX);
            state->layers_reported = true;
        }

        ecs_vec_set_count_t(NULL, &state->layers, box_layer_t, BOX_LAYER_MAX);
    }
}

const box_layer_t* box_render_layers(
    ecs_world_t *world,
    int32_t *count)
{
    *count = 0;
    const box_layer_t *result = NULL;
    ecs_entity_t id = ecs_lookup(world, "box.render.RenderState");
    if (!id) {
        goto done;
    }

    RenderState *state = ecs_get_mut_id(world, id, id);
    if (!state) {
        goto done;
    }

    if (state->layers_dirty && state->layer_query) {
        box_layer_rebuild(world, state);
        state->layers_dirty = false;
    }

    *count = ecs_vec_count(&state->layers);
    result = ecs_vec_first_t(&state->layers, box_layer_t);
done:
    return result;
}

uint32_t box_render_layer_mask(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    uint32_t result = 0;
    const box_layer_t *layers = ecs_vec_first_t(&state->layers, box_layer_t);
    int32_t count = ecs_vec_count(&state->layers);
    ecs_entity_t cur = e;
    while (cur) {
        int32_t index = 0;
        ecs_entity_t target;
        while ((target = ecs_get_target(world, cur, MaterialLayerOn, index))) {
            int32_t i;
            for (i = 0; i < count; i ++) {
                if (layers[i].entity == target) {
                    result |= 1u << i;
                }
            }

            index ++;
        }

        cur = ecs_get_target(world, cur, EcsIsA, 0);
    }

    return result;
}

void box_render_extract_layers(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    p->layer_count = 0;
    if (!state->layered) {
        goto done;
    }

    if (state->layers_dirty) {
        box_shader_layers_changed(world);
        box_render_mark_all(world, state);
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->layers); i ++) {
        const box_layer_t *entry = ecs_vec_get_t(&state->layers, box_layer_t, i);
        const LayerParams *params = ecs_get(world, entry->entity, LayerParams);
        LayerParams zero = {0};
        const LayerParams *v = params ? params : &zero;
        box_layer_params_t *out = &p->layer_params[i];
        out->a[0] = v->a.x;
        out->a[1] = v->a.y;
        out->a[2] = v->a.z;
        out->a[3] = v->a.w;
        out->b[0] = v->b.x;
        out->b[1] = v->b.y;
        out->b[2] = v->b.z;
        out->b[3] = v->b.w;
    }

    p->layer_count = (uint32_t)ecs_vec_count(&state->layers);
done:
    return;
}

/** A layer registration changed, so the list must be sent again.
 *
 * @param it Iterator over the changed layer entities
 */
static void box_layer_on_change(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->layers_dirty = true;
    }
}

/** A material enabled or disabled a layer, so its instances resolve a new mask.
 *
 * @param it Iterator over the changed material entities
 */
static void box_layer_on_material(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (!state) {
        goto done;
    }

    if (!state->layered) {
        state->layered = true;
        state->layers_dirty = true;
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_mark_subtree(it->world, it->entities[i]);
    }
done:
    return;
}

void box_render_layers_init(
    ecs_world_t *world)
{
    RenderState *state = ecs_singleton_ensure(world, RenderState);
    state->layer_query = ecs_query(world, {
        .terms = {{ .id = ecs_id(MaterialLayer), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(MaterialLayer) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_layer_on_change
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(MaterialLayerOn, EcsWildcard) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_layer_on_material
    });
}
