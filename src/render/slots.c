/**
 * @file render/slots.c
 * @brief Slots of the batches that no shape entity owns, the cards of the impostors, and the
 * lookups of the instance of a shape.
 *
 * The card of an instance of the prefab of a ready Impostor is a slot in the card batch of the
 * impostor at the transform of the instance, kept by the draws instead of by an entity, so cards
 * add no entities or tables. The shapes below the instance end where its card starts.
 */

#include "draw.h"

/** The card of an instance: its slot in the card batch of its impostor */
typedef struct box_draw_card_t {
    DrawSlot slot;          /**< Batch and slot of the card, with the fade and tints of the
                             *   instance and its ancestors */
    ecs_entity_t impostor;  /**< Entity with the Impostor whose atlases the card draws */
} box_draw_card_t;

/** The cards of an impostor */
typedef struct box_draw_impostor_t {
    ecs_entity_t card;      /**< Prefab with the mesh and material of the cards */
    DrawRange range;        /**< Range of the cards: near at Impostor.distance, its fade */
} box_draw_impostor_t;

void box_draws_cards_init(
    box_render_draws_t *d)
{
    ecs_map_init(&d->cards, NULL);
    ecs_map_init(&d->impostors, NULL);
    ecs_map_init(&d->card_sources, NULL);
    ecs_vec_init_t(NULL, &d->cards_dirty, ecs_entity_t, 0);
}

void box_draws_cards_fini(
    box_render_draws_t *d)
{
    ecs_map_iter_t it = ecs_map_iter(&d->cards);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    it = ecs_map_iter(&d->impostors);
    while (ecs_map_next(&it)) {
        ecs_os_free(ecs_map_ptr(&it));
    }

    ecs_map_fini(&d->cards);
    ecs_map_fini(&d->impostors);
    ecs_map_fini(&d->card_sources);
    ecs_vec_fini_t(NULL, &d->cards_dirty, ecs_entity_t);
}

/** Get the card of an instance.
 *
 * @param d The state
 * @param inst The instance
 * @return The card, NULL for none
 */
static box_draw_card_t* box_draws_card_get(
    const box_render_draws_t *d,
    ecs_entity_t inst)
{
    return d && ecs_map_count(&d->cards)
        ? ecs_map_get_deref(&d->cards, box_draw_card_t, inst)
        : NULL;
}

/** Add a card to the changed cards.
 *
 * @param d The state
 * @param inst The instance of the card
 * @param c The card
 * @param kinds box_draw_kind_t, the streams the change writes
 */
static void box_draws_card_dirty(
    box_render_draws_t *d,
    ecs_entity_t inst,
    box_draw_card_t *c,
    uint8_t kinds)
{
    c->slot.changed |= kinds;
    if (!c->slot.dirty) {
        c->slot.dirty = true;
        *ecs_vec_append_t(NULL, &d->cards_dirty, ecs_entity_t) = inst;
    }
}

/** Mark the cards of an impostor as changed, and the subtrees of their instances when the range
 * of the cards changed, which the shapes of the instances end at.
 *
 * @param d The state
 * @param impostor The entity with the Impostor
 * @param moved Whether the subtrees of the instances are marked too
 */
static void box_draws_cards_of(
    box_render_draws_t *d,
    ecs_entity_t impostor,
    bool moved)
{
    ecs_map_iter_t it = ecs_map_iter(&d->cards);
    while (ecs_map_next(&it)) {
        box_draw_card_t *c = ecs_map_ptr(&it);
        if (c->impostor != impostor) {
            continue;
        }

        box_draws_card_dirty(d, ecs_map_key(&it), c, BOX_DRAW_BOTH);
        if (moved) {
            *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
                .root = ecs_map_key(&it), .kinds = BOX_DRAW_ALL
            };
        }
    }
}

void box_draws_card_mark(
    box_render_draws_t *d,
    ecs_entity_t e,
    uint8_t kinds)
{
    box_draw_card_t *c = box_draws_card_get(d, e);
    ecs_map_val_t *impostor = c || !ecs_map_count(&d->card_sources)
        ? NULL : ecs_map_get(&d->card_sources, e);
    if (c) {
        box_draws_card_dirty(d, e, c, kinds);
    }

    if (impostor) {
        box_draws_cards_of(d, (ecs_entity_t)*impostor, false);
    }
}

DrawSlot* box_draws_slot_of(
    ecs_world_t *world,
    const box_render_draws_t *d,
    const box_draw_batch_t *b,
    ecs_entity_t e)
{
    box_draw_card_t *c = b->key.impostor ? box_draws_card_get(d, e) : NULL;

    return b->key.impostor ? (c ? &c->slot : NULL) : ecs_get_mut(world, e, DrawSlot);
}

const DrawRange* box_draws_card_range(
    const box_render_draws_t *d,
    ecs_entity_t e,
    const DrawRange *own,
    DrawRange *out)
{
    const box_draw_card_t *c = box_draws_card_get(d, e);
    const box_draw_impostor_t *imp = c
        ? ecs_map_get_deref(&d->impostors, box_draw_impostor_t, c->impostor)
        : NULL;
    const DrawRange *result = own;
    if (imp) {
        out->near_ = own ? own->near_ : 0.0f;
        out->far_ = imp->range.near_;
        out->fade = imp->range.fade;
        if (own && own->far_ > 0.0f && own->far_ < out->far_) {
            out->far_ = own->far_;
        }

        result = out;
    }

    return result;
}

/** Compose the fade of a card from the Opacity and the Tint of its instance and of the ancestors
 * of the instance, the nearest first, as box_draws_fade does for a child of the instance.
 *
 * @param world The world
 * @param inst The instance
 * @param tint Receives the composed tints
 * @return The product of the Opacity values
 */
static float box_draws_card_fade(
    const ecs_world_t *world,
    ecs_entity_t inst,
    box_draw_tint_t *tint)
{
    float result = 1.0f;
    ecs_entity_t cur;
    *tint = (box_draw_tint_t){ .keep = 1.0f };
    for (cur = inst; cur; cur = ecs_get_parent(world, cur)) {
        box_draws_fade_step(ecs_get(world, cur, Opacity), ecs_get(world, cur, Tint), &result,
            tint);
    }

    return result;
}

/** Compute the key of a card: an opaque, alpha tested mesh batch per impostor that draws its
 * atlases, in the scene of the instance, without textures, wind, layers or bakes.
 *
 * A card of an instance that moves every frame, or that moves with a mover, uploads its
 * transform every frame (dynamic_transform) instead of joining a mover batch.
 *
 * @param world The world
 * @param d The state
 * @param inst The instance
 * @param c The card
 * @param imp The cards of its impostor
 * @param shape The mesh of the cards
 * @param key Receives the key
 */
static void box_draws_card_key(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t inst,
    const box_draw_card_t *c,
    const box_draw_impostor_t *imp,
    const ShapeMesh *shape,
    box_draw_key_t *key)
{
    const ecs_table_t *table = ecs_get_table(world, inst);
    ecs_os_memset_t(key, 0, box_draw_key_t);
    key->scene = box_render_scene_of(world, inst);
    key->features = box_render_scene_features(world, key->scene);
    key->mesh = shape->render_mesh ? shape->render_mesh : shape->mesh;
    key->pipeline = BOX_RENDER_SHADER_MESH;
    key->impostor = c->impostor;
    key->blend = Opaque;
    key->cutout = true;
    key->wrap = WrapClamp;
    key->back_face = BackFacePlain;
    key->dynamic_transform = box_transform_dynamic(world, table) ||
        ecs_has_id(world, inst, TransitionTransform) ||
        box_draws_mover_table(world, table, false, ecs_pair(EcsIsA, DynamicTransformSpread));
    key->dynamic_material = ecs_get_target_for_id(world, inst, EcsChildOf, DynamicMaterial) != 0;
    key->shadow = !ecs_get_target_for_id(world, inst, EcsChildOf, NoShadow);
    key->fixture = key->shadow &&
        ecs_get_target_for_id(world, inst, EcsChildOf, LightFixture) != 0;
    key->range[0] = imp->range.near_;
    key->range[2] = imp->range.fade;
    if (key->features & BOX_RENDER_FEATURE_PLANET_LIGHT) {
        ecs_entity_t planet = box_draws_planet_of(world, inst);
        key->planet = planet ? box_draws_planet_slot(d, planet) : 0u;
    }
}

/** Write the streams of a card that changed and record the shadow regions of a caster.
 *
 * @param world The world
 * @param d The state
 * @param inst The instance
 * @param s The slot of the card
 * @param imp The cards of its impostor
 * @param material The material of the cards with the fade of the instance
 * @param kinds box_draw_kind_t, the streams to write
 * @param kept Whether the card kept its slot, else the slot is new
 */
static void box_draws_card_write(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t inst,
    const DrawSlot *s,
    const box_draw_impostor_t *imp,
    const box_draw_material_t *material,
    uint8_t kinds,
    bool kept)
{
    box_draw_batch_t *b = box_draws_batch(d, s->batch);
    box_render_transform_t *transform = box_draws_slot_transform(b, s->slot);
    box_render_transform_t was = *transform;
    bool record = d->shadow_record && box_draws_caster(b);
    WorldTransform buf;
    if (kinds & BOX_DRAW_TRANSFORM) {
        box_draws_transform(transform, box_transform_world(world, inst, &buf),
            ecs_get(world, imp->card, ShapeMesh));
    }

    if (kinds & BOX_DRAW_SHADING) {
        box_draws_mesh_shading(box_draws_slot_mesh_shading(b, s->slot), box_draws_planet_material(
            box_draws_material_id(world, d, imp->card, false, false), b->key.planet), material);
    }

    if (record && !kept) {
        box_draws_shadow_slot(world, d, b, s->slot);
    } else if (record && ((kinds & BOX_DRAW_SHADING) ||
        ecs_os_memcmp_t(&was, transform, box_render_transform_t)))
    {
        box_draws_shadow_region(world, d, b, &was, NULL);
        box_draws_shadow_region(world, d, b, transform, NULL);
    }

    box_draws_slot_changed(b, s->slot, kinds);
}

/** Bring a changed card into the batch of its key and write its instance, or give back its slot
 * when it is not drawn: while its instance is disabled, has no WorldTransform or is gone, or the
 * mesh of the cards is not resolved.
 *
 * @param world The world
 * @param d The state
 * @param inst The instance
 * @param c The card
 */
static void box_draws_card_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t inst,
    box_draw_card_t *c)
{
    DrawSlot *s = &c->slot;
    const box_draw_impostor_t *imp = ecs_map_get_deref(
        &d->impostors, box_draw_impostor_t, c->impostor);
    const ShapeMesh *shape = imp && ecs_is_alive(world, imp->card)
        ? ecs_get(world, imp->card, ShapeMesh) : NULL;
    bool drawn = shape && shape->mesh && ecs_is_alive(world, inst) &&
        ecs_has(world, inst, WorldTransform) && !ecs_has_id(world, inst, EcsPrefab) &&
        !ecs_has_id(world, inst, EcsDisabled);
    uint8_t kinds = s->changed;
    bool kept = false;
    box_draw_key_t key;
    box_draw_material_t material;
    s->dirty = false;
    s->changed = 0;
    d->stats.processed ++;
    if (drawn) {
        s->fade = box_draws_card_fade(world, inst, &s->tint);
        s->tinted = s->tint.keep < 1.0f;
        box_draws_shape_material(world, imp->card, s, &material);
        box_draws_card_key(world, d, inst, c, imp, shape, &key);
    }

    if (s->batch >= 0) {
        kept = drawn &&
            !ecs_os_memcmp_t(&box_draws_batch(d, s->batch)->key, &key, box_draw_key_t);
        if (!kept) {
            box_draws_slot_remove(world, d, s->batch, s->slot);
            s->batch = -1;
            s->slot = -1;
        }
    }

    if (drawn && !kept) {
        s->batch = box_draws_batch_ensure(d, &key);
        s->slot = box_draws_slot_add(d, s->batch, inst);
    }

    if (drawn) {
        kinds = kept ? (uint8_t)((kinds & BOX_DRAW_TRANSFORM) |
            ((kinds & ~BOX_DRAW_TRANSFORM) ? BOX_DRAW_SHADING : 0)) : (uint8_t)BOX_DRAW_BOTH;
        box_draws_card_write(world, d, inst, s, imp, &material, kinds, kept);
    }
}

void box_draws_cards_update(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i, s;
    for (i = 0; ecs_map_count(&d->cards) && i < ecs_vec_count(&d->batches); i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (!b->alive || !b->key.impostor ||
            !(b->key.dynamic_transform || b->key.dynamic_material))
        {
            continue;
        }

        for (s = 0; s < ecs_vec_count(&b->entities); s ++) {
            box_draws_card_mark(d, *ecs_vec_get_t(&b->entities, ecs_entity_t, s), BOX_DRAW_BOTH);
        }
    }

    for (i = 0; i < ecs_vec_count(&d->cards_dirty); i ++) {
        ecs_entity_t inst = *ecs_vec_get_t(&d->cards_dirty, ecs_entity_t, i);
        box_draw_card_t *c = box_draws_card_get(d, inst);
        if (c && c->slot.dirty) {
            box_draws_card_update(world, d, inst, c);
        }
    }

    ecs_vec_clear(&d->cards_dirty);
}

/** Remove the card of an instance, giving back its slot.
 *
 * @param world The world
 * @param d The state
 * @param inst The instance
 */
static void box_draws_card_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t inst)
{
    box_draw_card_t *c = box_draws_card_get(d, inst);
    if (!c) {
        goto done;
    }

    if (c->slot.batch >= 0) {
        box_draws_slot_remove(world, d, c->slot.batch, c->slot.slot);
    }

    ecs_map_remove_free(&d->cards, inst);
    if (ecs_is_alive(world, inst)) {
        *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
            .root = inst, .kinds = BOX_DRAW_ALL
        };
    }

done:
    return;
}

/** Remove the cards of an impostor and its record.
 *
 * @param world The world
 * @param d The state
 * @param impostor The entity with the Impostor
 * @param imp Its record
 */
static void box_draws_impostor_remove(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t impostor,
    box_draw_impostor_t *imp)
{
    ecs_vec_t insts;
    int32_t i;
    ecs_vec_init_t(NULL, &insts, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&d->cards);
    while (ecs_map_next(&it)) {
        if (((box_draw_card_t*)ecs_map_ptr(&it))->impostor == impostor) {
            *ecs_vec_append_t(NULL, &insts, ecs_entity_t) = ecs_map_key(&it);
        }
    }

    for (i = 0; i < ecs_vec_count(&insts); i ++) {
        box_draws_card_remove(world, d, *ecs_vec_get_t(&insts, ecs_entity_t, i));
    }

    ecs_vec_fini_t(NULL, &insts, ecs_entity_t);
    ecs_map_remove(&d->card_sources, imp->card);
    ecs_map_remove_free(&d->impostors, impostor);
}

void box_render_draws_impostor(
    ecs_world_t *world,
    ecs_entity_t impostor,
    ecs_entity_t card,
    float distance,
    float fade)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_impostor_t *imp = d
        ? ecs_map_get_deref(&d->impostors, box_draw_impostor_t, impostor) : NULL;
    bool changed = false, moved = false;
    if (!d || (!card && !imp)) {
        goto done;
    }

    if (!card) {
        box_draws_impostor_remove(world, d, impostor, imp);
        goto done;
    }

    if (!imp) {
        imp = ecs_map_insert_alloc_t(&d->impostors, box_draw_impostor_t, impostor);
    }

    if (imp->card != card) {
        ecs_map_remove(&d->card_sources, imp->card);
        ecs_map_insert(&d->card_sources, card, impostor);
        imp->card = card;
        changed = true;
    }

    if (imp->range.near_ != distance || imp->range.fade != fade) {
        imp->range = (DrawRange){ .near_ = distance, .fade = fade };
        moved = true;
    }

    if (changed || moved) {
        box_draws_cards_of(d, impostor, moved);
    }

done:
    return;
}

void box_render_draws_card_add(
    ecs_world_t *world,
    ecs_entity_t impostor,
    ecs_entity_t inst)
{
    box_render_draws_t *d = box_draws_state(world);
    box_draw_card_t *c = NULL;
    if (!d || ecs_map_get(&d->cards, inst) || !ecs_map_get(&d->impostors, impostor)) {
        goto done;
    }

    c = ecs_map_insert_alloc_t(&d->cards, box_draw_card_t, inst);
    c->slot = (DrawSlot){ .batch = -1, .slot = -1, .fade = 1.0f, .tint = { .keep = 1.0f } };
    c->impostor = impostor;
    box_draws_card_dirty(d, inst, c, BOX_DRAW_BOTH);
    *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
        .root = inst, .kinds = BOX_DRAW_ALL
    };

done:
    return;
}

void box_render_draws_card_remove(
    ecs_world_t *world,
    ecs_entity_t inst)
{
    box_render_draws_t *d = box_draws_state(world);
    if (d) {
        box_draws_card_remove(world, d, inst);
    }
}

const DrawSlot* box_draws_card(
    const box_render_draws_t *d,
    ecs_entity_t inst)
{
    const box_draw_card_t *c = box_draws_card_get(d, inst);

    return c ? &c->slot : NULL;
}

ecs_entity_t box_draws_card_source(
    const box_render_draws_t *d,
    ecs_entity_t impostor)
{
    const box_draw_impostor_t *imp = d
        ? ecs_map_get_deref(&d->impostors, box_draw_impostor_t, impostor) : NULL;

    return imp ? imp->card : 0;
}

bool box_render_draws_instance(
    const ecs_world_t *world,
    ecs_entity_t e,
    uint32_t *page,
    uint32_t *slot)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *ds = d && ecs_is_alive(world, e) ? ecs_get(world, e, DrawSlot) : NULL;
    const box_draw_batch_t *b = NULL;
    bool result = false;
    if (!ds || ds->batch < 0 || ds->batch >= ecs_vec_count(&d->batches)) {
        goto done;
    }

    b = ecs_vec_get_t(&d->batches, box_draw_batch_t, ds->batch);
    if (!b->alive || b->page < 0 || ds->slot < 0) {
        goto done;
    }

    *page = box_draws_store_handle(box_draws_batch_store(d, b), b->page,
        BOX_RENDER_STREAM_TRANSFORM);
    *slot = b->first + (uint32_t)ds->slot;
    result = true;
done:
    return result;
}

bool box_render_draws_instance_transform(
    const ecs_world_t *world,
    ecs_entity_t e,
    vec4 *rows)
{
    box_render_draws_t *d = box_draws_state(world);
    const DrawSlot *ds = d && ecs_is_alive(world, e) ? ecs_get(world, e, DrawSlot) : NULL;
    box_draw_batch_t *b = NULL;
    bool result = false;
    int32_t i;
    if (!ds || ds->batch < 0 || ds->batch >= ecs_vec_count(&d->batches)) {
        goto done;
    }

    b = ecs_vec_get_t(&d->batches, box_draw_batch_t, ds->batch);
    if (!b->alive || b->page < 0 || ds->slot < 0 || ds->slot >= ecs_vec_count(&b->transforms)) {
        goto done;
    }

    const box_render_transform_t *t = box_draws_slot_transform(b, ds->slot);
    for (i = 0; i < 3; i ++) {
        glm_vec4_copy(ECS_CONST_CAST(float*, t->rows[i]), rows[i]);
    }

    result = true;
done:
    return result;
}
