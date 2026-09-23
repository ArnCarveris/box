/**
 * @file effects/extract.c
 * @brief Builds the ordered effect chain of a view and copies its parameters.
 */

#include "effects_internal.h"

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
 * The parameters are the first reflected struct on the entity other than Effect,
 * flattened into shader floats. A component that is all floats is copied as is.
 *
 * @param world The world
 * @param state The registry
 * @param e The effect entity
 * @param component The parameter component to read, 0 for the first struct
 * @param out The parameter block to fill
 */
static void box_effect_params(
    ecs_world_t *world,
    EffectState *state,
    ecs_entity_t e,
    ecs_entity_t component,
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
        if (component && id != component) {
            continue;
        }

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
    if (!fx || !fx->shader || ecs_has_id(world, e, EcsPrefab) ||
        ecs_has_id(world, e, EcsDisabled))
    {
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
    pass->setup_shader = BOX_SHADER_NONE;
    pass->setup_kind = EffectSetupNone;
    pass->setup_format = EffectTextureRg16Float;
    memset(pass->setup_parameter, -1, sizeof(pass->setup_parameter));
    pass->setup_workgroup_size = 1;
    pass->setup_buffer_size[0] = 0;
    pass->setup_buffer_size[1] = 0;
    memset(pass->setup_initial, 0, sizeof(pass->setup_initial));
    const EffectSetup *setup = fx->setup ? ecs_get(world, fx->setup, EffectSetup) : NULL;
    const ShaderImpl *setup_impl = setup && setup->shader ?
        ecs_get(world, setup->shader, ShaderImpl) : NULL;
    if (setup && setup_impl && setup_impl->assigned) {
        pass->setup_shader = setup_impl->handle;
        pass->setup_kind = setup->kind;
        pass->setup_format = setup->format;
        pass->setup_parameter[0] = setup->parameter0;
        pass->setup_parameter[1] = setup->parameter1;
        pass->setup_parameter[2] = setup->parameter2;
        pass->setup_parameter[3] = setup->parameter3;
        pass->setup_workgroup_size = setup->workgroup_size > 0 ?
            (uint32_t)setup->workgroup_size : 1;
        pass->setup_buffer_size[0] = setup->buffer0_size > 0 ?
            (uint32_t)setup->buffer0_size : 0;
        pass->setup_buffer_size[1] = setup->buffer1_size > 0 ?
            (uint32_t)setup->buffer1_size : 0;
        pass->setup_initial[0] = setup->initial.x;
        pass->setup_initial[1] = setup->initial.y;
        pass->setup_initial[2] = setup->initial.z;
        pass->setup_initial[3] = setup->initial.w;
    }

    pass->scale = fx->scale > 0.0f ? fx->scale : 1.0f;
    pass->inputs = fx->color ? BOX_EFFECT_COLOR : 0u;
    pass->inputs |= fx->depth ? BOX_EFFECT_DEPTH : 0u;
    pass->inputs |= fx->normals ? BOX_EFFECT_NORMALS : 0u;
    pass->inputs |= fx->motion ? BOX_EFFECT_MOTION : 0u;
    box_effect_params(world, state, e, fx->params, pass->params);
    const EffectTexture *texture = fx->texture ?
        ecs_get(world, fx->texture, EffectTexture) : NULL;
    bool use_texture = texture && (texture->parameter < 0 ||
        (texture->parameter < BOX_EFFECT_PARAM_MAX &&
        pass->params[texture->parameter] == texture->value));
    pass->texture = BOX_EFFECT_TEXTURE_NONE;
    if (use_texture) {
        int32_t handle = box_effect_texture(world, state, fx->texture, p);
        if (handle >= 0) {
            pass->texture = (uint32_t)handle;
            pass->inputs |= BOX_EFFECT_TEXTURE;
        }
    }

    v->effect_inputs |= pass->inputs;
    v->effect_count ++;
done:
    return;
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
    const View *view = ecs_get(world, e, View);
    if (!view) {
        goto done;
    }

    if (!ecs_id(EffectState) ||
        ecs_lookup(world, "box.effects.EffectState") != ecs_id(EffectState))
    {
        goto done;
    }

    EffectState *state = ecs_get_mut(world, ecs_id(EffectState), EffectState);
    if (!state) {
        goto done;
    }

    int32_t count = ecs_vec_count(&view->effects);
    const ViewEffect *effects = ecs_vec_first_t(&view->effects, ViewEffect);
    int32_t i;
    for (i = 0; i < count && v->effect_count < BOX_EFFECTS_MAX; i ++) {
        if (effects[i].effect && effects[i].enabled) {
            box_effect_append(world, state, effects[i].effect, p, v);
        }
    }

done:
    return;
}
