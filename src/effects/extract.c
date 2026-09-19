/**
 * @file effects/extract.c
 * @brief Builds the ordered effect chain of a view and copies its parameters.
 *
 * The chain of a view is the list its View spells out, or every effect of the
 * world when it lists none. Only enabled effects with a compiled shader reach the
 * packet, so a disabled effect costs the renderer nothing and the inputs of the
 * view are the union of what its passes declared.
 */

#include "effects_internal.h"
#include "../sky/sky_internal.h"

/** Order two effects by their order and then by their entity.
 *
 * @param a The first effect
 * @param b The second effect
 * @return Negative when a runs first, positive when b does, zero when equal
 */
static int box_effect_compare(
    const void *a,
    const void *b)
{
    const box_effect_src_t *l = a;
    const box_effect_src_t *r = b;
    int result = (l->order > r->order) - (l->order < r->order);
    if (!result) {
        result = (l->entity > r->entity) - (l->entity < r->entity);
    }

    return result;
}

/** Read one reflected scalar as the float the shader block holds.
 *
 * @param kind Primitive kind of the value
 * @param ptr The value
 * @return The value as a float, 0 for a kind no parameter block carries
 */
static float box_effect_scalar(
    ecs_primitive_kind_t kind,
    const void *ptr)
{
    float result = 0.0f;
    switch (kind) {
    case EcsBool: result = *(const bool*)ptr ? 1.0f : 0.0f; break;
    case EcsU8: result = (float)*(const uint8_t*)ptr; break;
    case EcsU16: result = (float)*(const uint16_t*)ptr; break;
    case EcsU32: result = (float)*(const uint32_t*)ptr; break;
    case EcsI8: result = (float)*(const int8_t*)ptr; break;
    case EcsI16: result = (float)*(const int16_t*)ptr; break;
    case EcsI32: result = (float)*(const int32_t*)ptr; break;
    case EcsF32: result = *(const float*)ptr; break;
    case EcsF64: result = (float)*(const double*)ptr; break;
    default: break;
    }

    return result;
}

/** Append a reflected value to the parameter block as floats.
 *
 * Bools and integers reach the shader as floats and nested structs and inline
 * arrays are flattened, so the block holds the fields of a parameter component
 * tightly packed in the order its shader reads them.
 *
 * @param world The world
 * @param type Type of the value
 * @param ptr The value
 * @param out The parameter block
 * @param at Index of the next float, raised by what the value wrote
 * @param plain Cleared when a member is not a float, which a copy would miss
 */
static void box_effect_floats(
    ecs_world_t *world,
    ecs_entity_t type,
    const void *ptr,
    float *out,
    int32_t *at,
    bool *plain)
{
    const EcsPrimitive *primitive = ecs_get(world, type, EcsPrimitive);
    if (primitive) {
        if (*at < BOX_EFFECT_PARAM_MAX) {
            out[*at] = box_effect_scalar(primitive->kind, ptr);
        }

        plain[0] = plain[0] && primitive->kind == EcsF32;
        (*at) ++;
        goto done;
    }

    if (ecs_has(world, type, EcsEnum)) {
        if (*at < BOX_EFFECT_PARAM_MAX) {
            out[*at] = (float)*(const int32_t*)ptr;
        }

        plain[0] = false;
        (*at) ++;
        goto done;
    }

    const EcsStruct *desc = ecs_get(world, type, EcsStruct);
    if (!desc) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < ecs_vec_count(&desc->members); i ++) {
        const ecs_member_t *member = ecs_vec_get_t(&desc->members, ecs_member_t, i);
        const ecs_type_info_t *ti = ecs_get_type_info(world, member->type);
        if (!ti) {
            continue;
        }

        int32_t count = member->count > 1 ? member->count : 1;
        int32_t element;
        for (element = 0; element < count; element ++) {
            const char *value = (const char*)ptr + member->offset + element * ti->size;
            box_effect_floats(world, member->type, value, out, at, plain);
        }
    }

done:
    return;
}

/** Whether a parameter component was already flattened once.
 *
 * @param state The registry
 * @param id The parameter component
 * @return Its entry in the table, -1 when the component was not seen yet
 */
static int32_t box_effect_known(
    EffectState *state,
    ecs_id_t id)
{
    int32_t result = -1;
    int32_t i;
    for (i = 0; i < state->layout_count && result < 0; i ++) {
        if (state->layout_id[i] == id) {
            result = i;
        }
    }

    return result;
}

/** Remember how a parameter component reaches the block.
 *
 * @param state The registry
 * @param id The parameter component
 * @param raw A copy of the component is the block
 * @param size Bytes the component holds
 */
static void box_effect_remember(
    EffectState *state,
    ecs_id_t id,
    bool raw,
    int32_t size)
{
    if (state->layout_count < BOX_EFFECT_LAYOUT_MAX) {
        state->layout_id[state->layout_count] = id;
        state->layout_raw[state->layout_count] = raw;
        state->layout_size[state->layout_count] = size;
        state->layout_count ++;
    }
}

/** Copy the parameters of an effect out of its parameter component.
 *
 * The parameters are the first reflected struct on the entity that is not the
 * Effect itself, flattened into the floats the shader reads. A component whose
 * members are floats end to end is the block already, which the registry
 * remembers after it has flattened it once.
 *
 * @param world The world
 * @param state The registry
 * @param e The effect entity
 * @param out The parameter block to fill
 */
static void box_effect_params(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t e,
    float *out)
{
    memset(out, 0, BOX_EFFECT_PARAM_MAX * sizeof(float));
    const ecs_type_t *type = ecs_get_type(world, e);
    if (!type) {
        goto done;
    }

    int32_t i;
    for (i = 0; i < type->count; i ++) {
        ecs_id_t id = type->array[i];
        if (id & ECS_ID_FLAGS_MASK || id == ecs_id(Effect)) {
            continue;
        }

        if (!ecs_has(world, id, EcsStruct)) {
            continue;
        }

        const void *ptr = ecs_get_id(world, e, id);
        if (!ptr) {
            continue;
        }

        int32_t known = box_effect_known(state, id);
        if (known >= 0 && state->layout_raw[known]) {
            memcpy(out, ptr, (size_t)state->layout_size[known]);
            goto done;
        }

        int32_t at = 0;
        bool plain = true;
        box_effect_floats(world, id, ptr, out, &at, &plain);
        if (known < 0) {
            const ecs_type_info_t *ti = ecs_get_type_info(world, id);
            int32_t size = ti ? ti->size : 0;
            int32_t limit = BOX_EFFECT_PARAM_MAX * (int32_t)sizeof(float);
            bool raw = plain && size == at * (int32_t)sizeof(float) && size <= limit;
            box_effect_remember(state, id, raw, size);
        }

        goto done;
    }

done:
    return;
}

/** Append one effect entity to the chain of a view.
 *
 * @param world The world
 * @param state The registry
 * @param e The effect entity
 * @param p The packet
 * @param v The view
 */
static void box_effect_append(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t e,
    box_packet_t *p,
    box_view_t *v)
{
    const Effect *fx = ecs_get(world, e, Effect);
    if (!fx || !fx->enabled || !fx->shader) {
        goto done;
    }

    const ShaderImpl *impl = ecs_get(world, fx->shader, ShaderImpl);
    if (!impl || !impl->assigned) {
        goto done;
    }

    if (v->effect_count >= BOX_EFFECT_MAX) {
        goto done;
    }

    box_effect_t *pass = box_vec_push(&p->effects);
    pass->shader = impl->handle;
    pass->scale = fx->scale > 0.0f ? fx->scale : 1.0f;
    pass->inputs = fx->color ? BOX_EFFECT_COLOR : 0u;
    pass->inputs |= fx->depth ? BOX_EFFECT_DEPTH : 0u;
    pass->inputs |= fx->normals ? BOX_EFFECT_NORMALS : 0u;
    pass->inputs |= fx->motion ? BOX_EFFECT_MOTION : 0u;
    pass->inputs |= fx->pyramid ? BOX_EFFECT_PYRAMID : 0u;
    pass->inputs |= fx->exposure ? BOX_EFFECT_EXPOSURE : 0u;
    pass->inputs |= fx->occlusion ? BOX_EFFECT_OCCLUSION : 0u;
    box_effect_params(world, state, e, pass->params);
    if (ecs_has(world, e, Fog)) {
        box_sky_fog(world, v, pass->params);
    }

    const ToneMapping *tone = ecs_get(world, e, ToneMapping);
    if (tone && tone->curve == ToneMapTony) {
        if (box_effect_lut(state, p)) {
            pass->inputs |= BOX_EFFECT_LUT;
        } else {
            pass->params[BOX_EFFECT_TONE_CURVE] = (float)ToneMapNeutral;
        }
    }

    v->effect_inputs |= pass->inputs;
    v->effect_count ++;
done:
    return;
}

/** Collect the effects a view runs into the scratch stack, ordered.
 *
 * @param world The world
 * @param state The registry
 * @param e The View entity, 0 for the view a world without views gets
 */
static void box_effect_collect(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t e)
{
    ecs_vec_clear(&state->stack);
    const View *view = e ? ecs_get(world, e, View) : NULL;
    if (view && view->effect_list[0]) {
        int32_t i;
        for (i = 0; i < BOX_EFFECTS_MAX && view->effect_list[i]; i ++) {
            ecs_entity_t fx = view->effect_list[i];
            const Effect *effect = ecs_get(world, fx, Effect);
            if (!effect) {
                continue;
            }

            box_effect_src_t *src = ecs_vec_append_t(NULL, &state->stack, box_effect_src_t);
            src->entity = fx;
            src->order = effect->order;
        }
    } else {
        ecs_iter_t it = ecs_query_iter(world, state->effects);
        while (ecs_query_next(&it)) {
            Effect *effect = ecs_field(&it, Effect, 0);
            int32_t i;
            for (i = 0; i < it.count; i ++) {
                box_effect_src_t *src =
                    ecs_vec_append_t(NULL, &state->stack, box_effect_src_t);
                src->entity = it.entities[i];
                src->order = effect[i].order;
            }
        }
    }

    int32_t count = ecs_vec_count(&state->stack);
    qsort(ecs_vec_first_t(&state->stack, box_effect_src_t), (size_t)count,
        sizeof(box_effect_src_t), box_effect_compare);
}

void box_effects_extract(
    ecs_world_t *world,
    ecs_entity_t e,
    box_packet_t *p,
    box_view_t *v)
{
    v->effect_first = p->effects.count;
    v->effect_count = 0;
    v->effect_inputs = 0;
    if (!v->effects) {
        goto done;
    }

    if (!ecs_id(EffectState)) {
        goto done;
    }

    EffectState *state = ecs_get_mut(world, ecs_id(EffectState), EffectState);
    if (!state || !state->effects || !state->enabled) {
        goto done;
    }

    box_effect_collect(world, state, e);
    int32_t i;
    for (i = 0; i < ecs_vec_count(&state->stack); i ++) {
        box_effect_append(world, state,
            ecs_vec_get_t(&state->stack, box_effect_src_t, i)->entity, p, v);
    }

done:
    return;
}
