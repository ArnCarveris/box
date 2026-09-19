/**
 * @file render/extract_modulators.c
 * @brief Light modulator registry: the plugin list and the parameter block of each entry.
 *
 * A modulator is a LightModulator on a light or on a child of one. The index of
 * an entry in this list is the bit a light carries for it and the index of the
 * function the generated include of the mesh shader holds, so the list only
 * changes when a plugin is added, removed or points somewhere else. The
 * parameters travel every frame, read from whatever reflected component next to
 * the modulator was registered with box_light_modulator_params.
 */

#include "render_internal.h"
#include "../shader/shader_internal.h"

/** Light a modulator belongs to: the entity itself or its parent.
 *
 * @param world The world
 * @param e Entity holding the LightModulator
 * @return The light, 0 when neither the entity nor its parent is one
 */
static ecs_entity_t box_modulator_light(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_entity_t cur = e;
    int32_t step;
    for (step = 0; step < 2 && cur; step ++) {
        if (ecs_has(world, cur, DirectionalLight) || ecs_has(world, cur, PointLight) ||
            ecs_has(world, cur, SpotLight))
        {
            result = cur;
            break;
        }

        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Parameter component sitting next to a modulator, if it has one.
 *
 * @param world The world
 * @param e Entity holding the LightModulator
 * @return The component id, 0 when the entity carries no registered one
 */
static ecs_entity_t box_modulator_params_id(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0;
    ecs_entity_t cur = e;
    while (cur && !result) {
        const ecs_type_t *type = ecs_get_type(world, cur);
        int32_t i, count = type ? type->count : 0;
        for (i = 0; i < count; i ++) {
            ecs_id_t id = type->array[i];
            if (!(id & ECS_ID_FLAGS_MASK) && ecs_has_id(world, id, LightModulatorParams)) {
                result = id;
                break;
            }
        }

        cur = ecs_get_target(world, cur, EcsIsA, 0);
    }

    return result;
}

/** Rebuild the registration list from the modulator entities of the world.
 *
 * @param world The world
 * @param state The registry
 */
static void box_modulator_rebuild(
    ecs_world_t *world,
    RenderState *state)
{
    ecs_vec_clear(&state->modulators);
    ecs_iter_t it = ecs_query_iter(world, state->modulator_query);
    while (ecs_query_next(&it)) {
        LightModulator *modulator = ecs_field(&it, LightModulator, 0);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const LightModulator *m = &modulator[ecs_field_is_self(&it, 0) ? i : 0];
            ecs_entity_t light = box_modulator_light(world, it.entities[i]);
            if (!m->enabled || !m->shader || !light) {
                continue;
            }

            box_modulator_t *entry = ecs_vec_append_t(NULL, &state->modulators,
                box_modulator_t);
            entry->entity = it.entities[i];
            entry->light = light;
            entry->shader = m->shader;
            entry->params = box_modulator_params_id(world, it.entities[i]);
        }
    }

    int32_t count = ecs_vec_count(&state->modulators);
    if (count > BOX_MODULATOR_MAX) {
        if (!state->modulators_reported) {
            ecs_err("box.light: %d light modulators registered, only %d are applied",
                count, BOX_MODULATOR_MAX);
            state->modulators_reported = true;
        }

        ecs_vec_set_count_t(NULL, &state->modulators, box_modulator_t, BOX_MODULATOR_MAX);
    }
}

const box_modulator_t* box_render_modulators(
    ecs_world_t *world,
    int32_t *count)
{
    *count = 0;
    const box_modulator_t *result = NULL;
    ecs_entity_t id = ecs_lookup(world, "box.render.RenderState");
    if (!id) {
        goto done;
    }

    RenderState *state = ecs_get_mut_id(world, id, id);
    if (!state) {
        goto done;
    }

    if (state->modulators_dirty && state->modulator_query) {
        box_modulator_rebuild(world, state);
        state->modulators_dirty = false;
    }

    *count = ecs_vec_count(&state->modulators);
    result = ecs_vec_first_t(&state->modulators, box_modulator_t);
done:
    return result;
}

uint32_t box_render_modulator_mask(
    ecs_world_t *world,
    const RenderState *state,
    ecs_entity_t e)
{
    (void)world;
    uint32_t result = 0;
    const box_modulator_t *entries = ecs_vec_first_t(&state->modulators, box_modulator_t);
    int32_t i, count = ecs_vec_count(&state->modulators);
    for (i = 0; i < count; i ++) {
        if (entries[i].light == e) {
            result |= 1u << i;
        }
    }

    return result;
}

/** Append the floats a reflected type is made of to a parameter block.
 *
 * @param world The world
 * @param type The type to walk
 * @param ptr First byte of the value
 * @param out The block being filled
 * @param count Floats written so far, raised by this call
 */
static void box_modulator_walk(
    const ecs_world_t *world,
    ecs_entity_t type,
    const void *ptr,
    box_modulator_params_t *out,
    int32_t *count)
{
    const EcsPrimitive *primitive = ecs_get(world, type, EcsPrimitive);
    if (primitive) {
        if (*count < BOX_MODULATOR_PARAMS) {
            float value = 0;
            if (primitive->kind == EcsF32) {
                value = *(const float*)ptr;
            } else if (primitive->kind == EcsF64) {
                value = (float)*(const double*)ptr;
            } else if (primitive->kind == EcsI32) {
                value = (float)*(const int32_t*)ptr;
            } else if (primitive->kind == EcsU32) {
                value = (float)*(const uint32_t*)ptr;
            } else if (primitive->kind == EcsU8 || primitive->kind == EcsByte ||
                primitive->kind == EcsBool)
            {
                value = (float)*(const uint8_t*)ptr;
            }

            out->v[(*count) ++] = value;
        }

        goto done;
    }

    const EcsStruct *type_struct = ecs_get(world, type, EcsStruct);
    if (!type_struct) {
        goto done;
    }

    const ecs_member_t *members = ecs_vec_first_t(&type_struct->members, ecs_member_t);
    int32_t i, member_count = ecs_vec_count(&type_struct->members);
    for (i = 0; i < member_count; i ++) {
        box_modulator_walk(world, members[i].type,
            ECS_OFFSET(ptr, members[i].offset), out, count);
    }

done:
    return;
}

void box_render_extract_modulators(
    ecs_world_t *world,
    RenderState *state,
    box_packet_t *p)
{
    p->modulator_count = 0;
    memset(p->modulator_params, 0, sizeof(p->modulator_params));
    if (!state->modulated) {
        goto done;
    }

    bool dirty = state->modulators_dirty;
    int32_t count = 0;
    const box_modulator_t *entries = box_render_modulators(world, &count);
    if (dirty) {
        box_shader_modulators_changed(world);
    }

    int32_t i;
    for (i = 0; i < count; i ++) {
        if (!entries[i].params) {
            continue;
        }

        const void *value = ecs_get_id(world, entries[i].entity, entries[i].params);
        if (!value) {
            continue;
        }

        int32_t written = 0;
        box_modulator_walk(world, entries[i].params, value, &p->modulator_params[i],
            &written);
    }

    p->modulator_count = (uint32_t)count;
done:
    return;
}

/** A modulator was written or removed, so the list and the mesh shader change.
 *
 * @param it Iterator over the changed modulator entities
 */
static void box_modulator_on_change(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (state) {
        state->modulators_dirty = true;
        state->modulated = true;
    }
}

/** A shader was preprocessed again, which recomposes the mesh when it is a plugin.
 *
 * @param it Iterator over the shader entities whose status was written
 */
static void box_modulator_on_shader(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    if (!state || !state->modulated) {
        goto done;
    }

    const box_modulator_t *entries = ecs_vec_first_t(&state->modulators, box_modulator_t);
    int32_t i, j, count = ecs_vec_count(&state->modulators);
    for (i = 0; i < it->count; i ++) {
        for (j = 0; j < count; j ++) {
            if (entries[j].shader == it->entities[i]) {
                state->modulators_dirty = true;
            }
        }
    }

done:
    return;
}

void box_render_modulators_init(
    ecs_world_t *world)
{
    RenderState *state = ecs_singleton_ensure(world, RenderState);
    state->modulator_query = ecs_query(world, {
        .terms = {{ .id = ecs_id(LightModulator), .inout = EcsIn }},
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(LightModulator) }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet, EcsOnRemove },
        .callback = box_modulator_on_change
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(ShaderStatus), .inout = EcsIn }},
        .query.flags = EcsQueryMatchPrefab | EcsQueryMatchDisabled,
        .events = { EcsOnSet },
        .callback = box_modulator_on_shader
    });
}
