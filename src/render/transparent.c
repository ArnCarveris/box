/**
 * @file render/transparent.c
 * @brief Sorted draw list: the instances the batched opaque draws cannot show.
 *
 * An instance lands here when it blends, when it is translucent or when it
 * shows its back faces; the list is rebuilt and sorted back to front per frame.
 */

#include "render_internal.h"

/** Entries the sorted list orders by insertion instead of by radix passes */
#define BOX_SORT_SMALL (32)

uint32_t box_render_blend_mode(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const BlendMode *mode = ecs_get(world, e, BlendMode);

    return mode ? (uint32_t)*mode : (uint32_t)BOX_BLEND_OPAQUE;
}

uint32_t box_render_back_mode(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const BackFaceMode *mode = ecs_get(world, e, BackFaceMode);

    return mode ? (uint32_t)*mode : (uint32_t)BOX_BACK_CULL;
}

/** Opacity of an entity times the alpha of its resolved tint.
 *
 * @param world The world
 * @param e The entity
 * @return The effective alpha, 1 when neither component is present
 */
static float box_render_alpha(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const Opacity *opacity = ecs_get(world, e, Opacity);
    const ActualTint *tint = ecs_get(world, e, ActualTint);
    float result = opacity ? opacity->value : 1.0f;
    if (tint) {
        result *= (float)tint->a / 255.0f;
    }

    return result;
}

bool box_render_is_transparent(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    bool result = false;
    if (state->blended) {
        result = box_render_blend_mode(world, e) != BOX_BLEND_OPAQUE ||
            box_render_back_mode(world, e) != BOX_BACK_CULL;
    }

    if (!result && state->translucent) {
        result = box_render_alpha(world, e) < 1.0f;
    }

    return result;
}

void box_render_set_transparent(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    RenderInstance *inst,
    bool transparent)
{
    bool listed = box_render_in_list(state, e, inst);
    if (transparent == listed) {
        inst->transparent = transparent;
        goto done;
    }

    if (transparent) {
        inst->sorted = (uint32_t)ecs_vec_count(&state->transparent);
        ecs_vec_append_t(NULL, &state->transparent, ecs_entity_t)[0] = e;
    } else {
        int32_t last = ecs_vec_count(&state->transparent) - 1;
        ecs_entity_t *entities = ecs_vec_first_t(&state->transparent, ecs_entity_t);
        ecs_entity_t moved = entities[last];
        entities[inst->sorted] = moved;
        ecs_vec_remove_last(&state->transparent);
        if (moved != e) {
            RenderInstance *moved_inst = ecs_get_mut(world, moved, RenderInstance);
            ecs_assert(moved_inst != NULL, ECS_INTERNAL_ERROR, NULL);
            moved_inst->sorted = inst->sorted;
        }
    }

    inst->transparent = transparent;
done:
    return;
}

/** Turn a view space depth into an unsigned key that orders back to front.
 *
 * @param depth The view space depth
 * @return The key, smaller for an instance farther from the camera
 */
static uint32_t box_render_sort_bits(
    float depth)
{
    uint32_t bits;
    memcpy(&bits, &depth, sizeof(bits));
    bits ^= (bits & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;

    return ~bits;
}

/** Order a short run of sort keys by insertion.
 *
 * @param keys The keys
 * @param count Keys in the array
 */
static void box_render_sort_small(
    box_sort_key_t *keys,
    int32_t count)
{
    int32_t i;
    for (i = 1; i < count; i ++) {
        box_sort_key_t key = keys[i];
        int32_t j = i - 1;
        while (j >= 0 && keys[j].key > key.key) {
            keys[j + 1] = keys[j];
            j --;
        }

        keys[j + 1] = key;
    }
}

/** Order the sort keys with four byte wide radix passes.
 *
 * @param keys The keys, sorted in place
 * @param tmp Scratch array of the same length
 * @param count Keys in the arrays
 */
static void box_render_sort_radix(
    box_sort_key_t *keys,
    box_sort_key_t *tmp,
    int32_t count)
{
    int32_t shift;
    for (shift = 0; shift < 32; shift += 8) {
        uint32_t offsets[256] = {0};
        int32_t i;
        for (i = 0; i < count; i ++) {
            offsets[(keys[i].key >> shift) & 0xFF] ++;
        }

        uint32_t total = 0;
        int32_t bucket;
        for (bucket = 0; bucket < 256; bucket ++) {
            uint32_t bucket_count = offsets[bucket];
            offsets[bucket] = total;
            total += bucket_count;
        }

        for (i = 0; i < count; i ++) {
            tmp[offsets[(keys[i].key >> shift) & 0xFF] ++] = keys[i];
        }

        box_sort_key_t *swap = keys;
        keys = tmp;
        tmp = swap;
    }
}

/** Check whether the frustum of a view holds the bounds of a sorted instance.
 *
 * @param world The world
 * @param e The entity
 * @param frustum The frustum of the view
 * @return True when the view can see the entity
 */
static bool box_render_sort_visible(
    ecs_world_t *world,
    ecs_entity_t e,
    const box_frustum_t *frustum)
{
    const RenderMesh *mesh = ecs_get(world, e, RenderMesh);
    const WorldTransform *transform = ecs_get(world, e, WorldTransform);
    ecs_assert(mesh != NULL, ECS_INTERNAL_ERROR, NULL);
    mat4 scale = mat4_scaling(mesh->scale);
    mat4 m = mat4_mul(&transform->m, &scale);
    float bounds[4];
    box_render_instance_bounds(mesh, &m, bounds);

    return box_frustum_sphere(frustum, bounds, bounds[3]);
}

/** Fill the sort keys with the view space depth of every instance the view sees.
 *
 * Instances outside the frustum are left out before the keys are ordered, so
 * the sort and the packet only carry what the view draws.
 *
 * @param world The world
 * @param state The registry
 * @param v The view the draws are sorted for
 */
static void box_render_sort_keys(
    ecs_world_t *world,
    RenderState *state,
    const box_view_t *v)
{
    mat4 view;
    memcpy(view.v, v->view, sizeof(view.v));
    int32_t total = ecs_vec_count(&state->transparent);
    ecs_vec_set_count_t(NULL, &state->sort, box_sort_key_t, total);
    box_sort_key_t *keys = ecs_vec_first_t(&state->sort, box_sort_key_t);
    ecs_entity_t *entities = ecs_vec_first_t(&state->transparent, ecs_entity_t);
    box_frustum_t frustum;
    if (v->cull_enabled) {
        box_frustum_of_view(v, &frustum);
    }

    int32_t count = 0;
    int32_t i;
    for (i = 0; i < total; i ++) {
        if (v->cull_enabled && !box_render_sort_visible(world, entities[i], &frustum)) {
            continue;
        }

        const WorldTransform *transform = ecs_get(world, entities[i], WorldTransform);
        ecs_assert(transform != NULL, ECS_INTERNAL_ERROR, NULL);
        vec3 origin = mat4_transform_point(&view, mat4_get_translation(&transform->m));
        keys[count].depth = origin.z;
        keys[count].key = box_render_sort_bits(origin.z);
        keys[count].index = (uint32_t)i;
        count ++;
    }

    ecs_vec_set_count_t(NULL, &state->sort, box_sort_key_t, count);
    if (count <= BOX_SORT_SMALL) {
        box_render_sort_small(keys, count);
    } else {
        ecs_vec_set_count_t(NULL, &state->sort_tmp, box_sort_key_t, count);
        box_render_sort_radix(keys, ecs_vec_first_t(&state->sort_tmp, box_sort_key_t), count);
    }
}

void box_render_extract_transparent(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p,
    box_view_t *v)
{
    v->transparent_first = p->transparent.count;
    v->transparent_count = 0;
    if (!ecs_vec_count(&state->transparent)) {
        goto done;
    }

    box_render_sort_keys(world, state, v);
    box_sort_key_t *keys = ecs_vec_first_t(&state->sort, box_sort_key_t);
    ecs_entity_t *entities = ecs_vec_first_t(&state->transparent, ecs_entity_t);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->sort); i ++) {
        ecs_entity_t e = entities[keys[i].index];
        if (v->layer_mask != BOX_VIEW_LAYER_ALL || v->strict_layers) {
            const RenderInstance *inst = ecs_get(world, e, RenderInstance);
            if (inst && !box_view_shows(v, inst->layer_mask)) {
                continue;
            }
        }

        const RenderMesh *mesh = ecs_get(world, e, RenderMesh);
        const WorldTransform *transform = ecs_get(world, e, WorldTransform);
        ecs_assert(mesh != NULL, ECS_INTERNAL_ERROR, NULL);

        box_transparent_t *entry = box_vec_push(&p->transparent);
        entry->mesh = mesh->handle;
        entry->depth = keys[i].depth;
        box_render_fill_instance(world, e, mesh, transform, false, state, &entry->data);
        const RenderInstance *inst = ecs_get(world, e, RenderInstance);
        if (inst && !box_view_casts(v, inst->layer_mask)) {
            box_instance_set_caster(&entry->data, false);
        }

        entry->blend = box_render_blend_mode(world, e);
        entry->back = box_render_back_mode(world, e);
        if (entry->blend == BOX_BLEND_OPAQUE && entry->data.color[3] < 1.0f) {
            entry->blend = BOX_BLEND_ALPHA;
        }

        box_instance_set_shading(&entry->data, entry->blend, entry->back);
    }

    v->transparent_count = p->transparent.count - v->transparent_first;
done:
    return;
}
