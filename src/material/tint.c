/**
 * @file material/tint.c
 * @brief Composes Tint down the hierarchy into the ActualTint of a subtree.
 *
 * A tint mixes the surface color toward its color by its strength, and a chain
 * of such mixes is itself one mix: the strengths combine into one minus the
 * product of what each level leaves, the colors into the contribution they add.
 */

#include "material_internal.h"
#include "../transform/transform_internal.h"

/** Convert a normalized channel to a display byte.
 *
 * @param value The channel, 0 to 1
 * @return The rounded byte
 */
static uint8_t box_tint_byte(
    float value)
{
    float clamped = value;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }

    if (clamped > 1.0f) {
        clamped = 1.0f;
    }

    return (uint8_t)(clamped * 255.0f + 0.5f);
}

/** Mix one channel of a tint on top of the channel an entity inherits.
 *
 * @param base The inherited channel
 * @param base_a The inherited strength, 0 to 1
 * @param tint The channel of the tint
 * @param a The strength of the tint, 0 to 1
 * @param alpha The composed strength, above zero
 * @return The composed channel
 */
static uint8_t box_tint_mix(
    uint8_t base,
    float base_a,
    uint8_t tint,
    float a,
    float alpha)
{
    float kept = (float)base / 255.0f * base_a * (1.0f - a);
    float added = (float)tint / 255.0f * a;

    return box_tint_byte((kept + added) / alpha);
}

/** Compose the tint of an entity with the tint it inherits.
 *
 * @param base The inherited tint, NULL when no ancestor is tinted
 * @param tint The tint of the entity
 * @return The composed tint
 */
static ActualTint box_tint_compose(
    const ActualTint *base,
    const Tint *tint)
{
    float a = (float)tint->a / 255.0f;
    float base_a = base ? (float)base->a / 255.0f : 0.0f;
    float alpha = 1.0f - (1.0f - base_a) * (1.0f - a);
    ActualTint result = { tint->r, tint->g, tint->b, box_tint_byte(alpha) };
    if (alpha <= 0.0f) {
        goto done;
    }

    result.r = box_tint_mix(base ? base->r : 0, base_a, tint->r, a, alpha);
    result.g = box_tint_mix(base ? base->g : 0, base_a, tint->g, a, alpha);
    result.b = box_tint_mix(base ? base->b : 0, base_a, tint->b, a, alpha);
done:
    return result;
}

/** Resolve the tint of an entity and of every descendant under it.
 *
 * @param world The world, used for reading
 * @param stage The stage that queues the writes
 * @param e The entity
 * @param inherited The tint of the parent, NULL when the parent has none
 * @param compose Mix the Tint of the entity in, false when it is being removed
 */
static void box_tint_apply(
    ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t e,
    const ActualTint *inherited,
    bool compose)
{
    ActualTint value = {0};
    bool tinted = inherited != NULL;
    if (tinted) {
        value = *inherited;
    }

    const Tint *tint = compose ? ecs_get(world, e, Tint) : NULL;
    if (tint) {
        value = box_tint_compose(tinted ? &value : NULL, tint);
        tinted = true;
    }

    if (tinted) {
        ecs_set_ptr(stage, e, ActualTint, &value);
    } else {
        ecs_remove(stage, e, ActualTint);
    }

    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    box_hierarchy_collect(world, e, &children);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&children); i ++) {
        ecs_entity_t child = ecs_vec_get_t(&children, ecs_entity_t, i)[0];
        box_tint_apply(world, stage, child, tinted ? &value : NULL, true);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

/** Get the resolved tint an entity inherits from its parent.
 *
 * @param world The world
 * @param e The entity
 * @return The tint of the parent, NULL when it has none
 */
static const ActualTint* box_tint_inherited(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t parent = box_hierarchy_parent(world, e);

    return parent ? ecs_get(world, parent, ActualTint) : NULL;
}

/** Tint was set: resolve it for the subtree.
 *
 * @param it Iterator over the tinted entities
 */
static void box_tint_on_set(
    ecs_iter_t *it)
{
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const ActualTint *inherited = box_tint_inherited(it->world, it->entities[i]);
        box_tint_apply(it->world, it->stage, it->entities[i], inherited, true);
    }

}

/** Tint was removed: resolve the subtree from the tint of the parent again.
 *
 * @param it Iterator over the entities that lost their tint
 */
static void box_tint_on_remove(
    ecs_iter_t *it)
{
    bool deleting = !it->other_table || !ecs_table_get_type(it->other_table)->count;
    int32_t i;
    for (i = 0; i < it->count && !deleting; i ++) {
        const ActualTint *inherited = box_tint_inherited(it->world, it->entities[i]);
        box_tint_apply(it->world, it->stage, it->entities[i], inherited, false);
    }

}

/** Entity was parented under a tinted ancestor in either storage: compose its tint.
 *
 * @param it Iterator over the reparented entities
 */
static void box_tint_on_parent(
    ecs_iter_t *it)
{
    ActualTint *parent_tint = ecs_field(it, ActualTint, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_tint_apply(it->world, it->stage, it->entities[i], parent_tint, true);
    }

}

/** A tinted entity gained or lost a parent in either storage: resolve it again.
 *
 * @param it Iterator over the reparented entities
 */
static void box_tint_on_reparent(
    ecs_iter_t *it)
{
    bool unparented = it->event == EcsOnRemove;
    if (unparented) {
        if (!it->other_table || !ecs_table_get_type(it->other_table)->count) {
            goto done;
        }

        if (box_hierarchy_is_child(it->world, it->other_table)) {
            goto done;
        }
    }

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const ActualTint *inherited = unparented ?
            NULL : box_tint_inherited(it->world, it->entities[i]);
        box_tint_apply(it->world, it->stage, it->entities[i], inherited, true);
    }

done:
    return;
}

void box_tint_init(
    ecs_world_t *world)
{
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnSet },
        .callback = box_tint_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnRemove },
        .callback = box_tint_on_remove
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard) },
            { .id = ecs_id(ActualTint), .src.id = EcsUp, .inout = EcsIn },
            { .id = ecs_id(ActualTint), .oper = EcsNot }
        },
        .events = { EcsOnAdd },
        .callback = box_tint_on_parent
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_pair(EcsChildOf, EcsWildcard) },
            { .id = ecs_id(ActualTint), .inout = EcsInOutFilter }
        },
        .events = { EcsOnAdd, EcsOnRemove },
        .callback = box_tint_on_reparent
    });
}
