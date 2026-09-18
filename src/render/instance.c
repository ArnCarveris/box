/**
 * @file render/instance.c
 * @brief Tracks which entities need extraction and manages static instance slots.
 */

#include "render_internal.h"

void box_render_mark_dirty(
    ecs_world_t *world,
    ecs_entity_t e)
{
    RenderState *state = ecs_get_mut(world, ecs_id(RenderState), RenderState);
    if (!state) {
        goto done;
    }

    RenderInstance *inst = ecs_get_mut(world, e, RenderInstance);
    ecs_assert(inst != NULL, ECS_INTERNAL_ERROR, "RenderMesh entity without RenderInstance");
    if (!inst->dirty) {
        inst->dirty = true;
        ecs_vec_append_t(NULL, &state->dirty, ecs_entity_t)[0] = e;
    }
done:
    return;
}

/** Queue an entity and every descendant with a mesh for extraction.
 *
 * @param world The world
 * @param e The entity
 */
static void box_render_mark_subtree(
    ecs_world_t *world,
    ecs_entity_t e)
{
    if (ecs_has(world, e, RenderMesh)) {
        box_render_mark_dirty(world, e);
    }

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            box_render_mark_subtree(world, it.entities[i]);
        }
    }
}

/** A component that feeds instance data was set.
 *
 * @param it Iterator over the changed entities
 */
static void box_render_on_change(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_mark_dirty(it->world, it->entities[i]);
    }
}

/** A dynamic tag was added or removed: re-evaluate the subtree.
 *
 * @param it Iterator over the changed entities
 */
static void box_render_on_dynamic(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_mark_subtree(it->world, it->entities[i]);
    }
}

void box_render_release_slot(
    ecs_world_t *world,
    RenderState *state,
    ecs_entity_t e,
    RenderInstance *inst)
{
    box_batch_t *batch = ecs_vec_get_t(&state->batches, box_batch_t, (int32_t)inst->mesh);
    int32_t last = ecs_vec_count(&batch->entities) - 1;
    ecs_entity_t *slots = ecs_vec_first_t(&batch->entities, ecs_entity_t);
    ecs_assert(slots[inst->slot] == e, ECS_INTERNAL_ERROR, NULL);

    ecs_entity_t moved = slots[last];
    slots[inst->slot] = moved;
    ecs_vec_remove_last(&batch->entities);
    batch->count_dirty = true;

    if (moved != e) {
        RenderInstance *moved_inst = ecs_get_mut(world, moved, RenderInstance);
        ecs_assert(moved_inst != NULL, ECS_INTERNAL_ERROR, NULL);
        moved_inst->slot = inst->slot;
        box_render_mark_dirty(world, moved);
    }
}

/** An instance record is being removed: free its static slot.
 *
 * @param it Iterator over the removed entities
 */
static void box_render_on_remove(
    ecs_iter_t *it)
{
    RenderState *state = ecs_get_mut(it->world, ecs_id(RenderState), RenderState);
    RenderInstance *inst = ecs_field(it, RenderInstance, 0);
    int32_t i;
    for (i = 0; state && i < it->count; i ++) {
        if (!inst[i].dynamic && inst[i].mesh < (uint32_t)ecs_vec_count(&state->batches)) {
            box_render_release_slot(it->world, state, it->entities[i], &inst[i]);
        }
    }
}

/** Register an OnSet observer for a component on entities with a mesh.
 *
 * @param world The world
 * @param component The component to watch
 */
static void box_render_watch(
    ecs_world_t *world,
    ecs_entity_t component)
{
    ecs_observer(world, {
        .query.terms = {
            { .id = component },
            { .id = ecs_id(RenderMesh), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = box_render_on_change
    });
}

/** Convert a color and tint to linear rgba.
 *
 * @param color The base color, NULL for white
 * @param tint The resolved tint, NULL for none
 * @param opacity The opacity
 * @param out Four floats receiving the result
 */
static void box_render_color(
    const Rgb *color,
    const ActualTint *tint,
    float opacity,
    float *out)
{
    out[0] = color ? box_srgb_to_linear(color->r) : 1.0f;
    out[1] = color ? box_srgb_to_linear(color->g) : 1.0f;
    out[2] = color ? box_srgb_to_linear(color->b) : 1.0f;
    out[3] = opacity;
    if (tint) {
        out[0] *= (float)tint->r / 255.0f;
        out[1] *= (float)tint->g / 255.0f;
        out[2] *= (float)tint->b / 255.0f;
        out[3] *= (float)tint->a / 255.0f;
    }
}

void box_render_fill_instance(
    ecs_world_t *world,
    ecs_entity_t e,
    const RenderMesh *mesh,
    const WorldTransform *transform,
    box_instance_t *out)
{
    mat4 scale = mat4_scaling(mesh->scale);
    mat4 m = mat4_mul(&transform->m, &scale);
    int row;
    for (row = 0; row < 3; row ++) {
        out->transform[row * 4 + 0] = m.v[0 + row];
        out->transform[row * 4 + 1] = m.v[4 + row];
        out->transform[row * 4 + 2] = m.v[8 + row];
        out->transform[row * 4 + 3] = m.v[12 + row];
    }

    const Opacity *opacity = ecs_get(world, e, Opacity);
    box_render_color(ecs_get(world, e, Rgb), ecs_get(world, e, ActualTint),
        opacity ? opacity->value : 1.0f, out->color);

    const Emissive *emissive = ecs_get(world, e, Emissive);
    float strength = emissive ? emissive->strength : 0.0f;
    out->emissive[0] = emissive ? box_srgb_to_linear(emissive->color.r) * strength : 0.0f;
    out->emissive[1] = emissive ? box_srgb_to_linear(emissive->color.g) * strength : 0.0f;
    out->emissive[2] = emissive ? box_srgb_to_linear(emissive->color.b) * strength : 0.0f;
    out->emissive[3] = 0.0f;

    const PbrMaterial *pbr = ecs_get(world, e, PbrMaterial);
    out->params[0] = pbr ? pbr->metallic : 0.0f;
    out->params[1] = pbr ? pbr->roughness : 0.5f;
    out->params[2] = 0.0f;
    out->params[3] = 0.0f;
}

void box_render_instance_init(
    ecs_world_t *world)
{
    box_render_watch(world, ecs_id(RenderMesh));
    box_render_watch(world, ecs_id(WorldTransform));
    box_render_watch(world, ecs_id(Rgb));
    box_render_watch(world, ecs_id(Opacity));
    box_render_watch(world, ecs_id(ActualTint));
    box_render_watch(world, ecs_id(PbrMaterial));
    box_render_watch(world, ecs_id(Emissive));

    ecs_observer(world, {
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

    ecs_observer(world, {
        .query.terms = {{ .id = DynamicMaterial }},
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_render_on_dynamic
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(RenderInstance) }},
        .events = { EcsOnRemove },
        .callback = box_render_on_remove
    });
}
