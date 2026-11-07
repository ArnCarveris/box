/**
 * @file render/materials.c
 * @brief The material table: GPU records shared by content with reference counts, and private
 * records of prefabs and of the shapes whose record changes every frame.
 *
 * Mesh shapes that own a MaterialId share the slot of the record their material packs to, found
 * by its hash; slot 0, the default material, is never freed. A material change hashes its owners
 * in the next extract, and a frame without one hashes nothing (box_draws_materials_resolve).
 *
 * Prefabs, shapes of DynamicMaterial batches and shapes with a running material transition own
 * a private slot, packed from its owner in the send that uploads it, so a prefab change uploads
 * one record and transitions.c can patch the record by slot (box_materials_transitions_end).
 */

#include "draw.h"

/** Slot of the default material */
#define BOX_MATERIALS_DEFAULT (0u)

/** No slot */
#define BOX_MATERIALS_NONE (UINT32_MAX)

/** Bit of an owned value: the owner shares a record and its material changed since it was
 * hashed, so the next resolve hashes it again */
#define BOX_MATERIALS_PENDING (1ull << 32)

/** Flags of a slot of the material table */
typedef enum box_materials_flag_t {
    BOX_MATERIALS_MARKED = 1 << 0,      /**< The slot is in dirty */
    BOX_MATERIALS_LISTED = 1 << 1,      /**< The slot is the one contents holds for its hash */
    BOX_MATERIALS_DYNAMIC = 1 << 2,     /**< Private for a shape of a DynamicMaterial batch */
    BOX_MATERIALS_TRANSITION = 1 << 3,  /**< Private for a running material transition */
    BOX_MATERIALS_TRANSITING = 1 << 4   /**< The slot is in transiting */
} box_materials_flag_t;

/** Flags a slot keeps when it changes hands: the lists it is in */
#define BOX_MATERIALS_LISTS (BOX_MATERIALS_MARKED | BOX_MATERIALS_TRANSITING)

/** Get a slot of the table.
 *
 * @param m The table
 * @param slot The slot
 * @return Its state
 */
static box_draw_material_slot_t* box_materials_slot(
    box_draw_materials_t *m,
    uint32_t slot)
{
    return ecs_vec_get_t(&m->slots, box_draw_material_slot_t, (int32_t)slot);
}

/** Get the record of a slot.
 *
 * @param m The table
 * @param slot The slot
 * @return The record
 */
static box_render_material_t* box_materials_record(
    box_draw_materials_t *m,
    uint32_t slot)
{
    return ecs_vec_get_t(&m->records, box_render_material_t, (int32_t)slot);
}

/** Pack a slot again in the next send, unless it is already queued.
 *
 * @param m The table
 * @param slot The slot
 */
static void box_materials_mark(
    box_draw_materials_t *m,
    uint32_t slot)
{
    box_draw_material_slot_t *s = box_materials_slot(m, slot);
    if (!(s->flags & BOX_MATERIALS_MARKED)) {
        s->flags |= BOX_MATERIALS_MARKED;
        *ecs_vec_append_t(NULL, &m->dirty, uint32_t) = slot;
    }
}

/** Hash a packed record.
 *
 * @param record The record
 * @return The hash
 */
static uint64_t box_materials_hash(
    const box_render_material_t *record)
{
    const uint32_t *words = (const uint32_t*)record;
    uint64_t h = 0xCBF29CE484222325ull;
    int32_t i;
    for (i = 0; i < (int32_t)(sizeof(box_render_material_t) / sizeof(uint32_t)); i ++) {
        h = (h ^ words[i]) * 0x100000001B3ull;
        h ^= h >> 29;
    }

    return h ^ (h >> 32);
}

/** Find the shared slot of a content.
 *
 * @param m The table
 * @param hash Hash of the record
 * @param record The record
 * @return The slot, BOX_MATERIALS_NONE when no listed slot holds the content
 */
static uint32_t box_materials_find(
    box_draw_materials_t *m,
    uint64_t hash,
    const box_render_material_t *record)
{
    ecs_map_val_t *found = ecs_map_get(&m->contents, (ecs_map_key_t)hash);
    uint32_t result = BOX_MATERIALS_NONE;
    if (found && !ecs_os_memcmp_t(box_materials_record(m, (uint32_t)*found), record,
        box_render_material_t))
    {
        result = (uint32_t)*found;
    }

    return result;
}

/** List a shared slot under the hash of its record, unless another slot holds the hash: a
 * content whose hash collides keeps a slot that is not found.
 *
 * @param m The table
 * @param slot The slot
 * @param hash Hash of its record
 */
static void box_materials_list(
    box_draw_materials_t *m,
    uint32_t slot,
    uint64_t hash)
{
    box_draw_material_slot_t *s = box_materials_slot(m, slot);
    s->hash = hash;
    if (!ecs_map_get(&m->contents, (ecs_map_key_t)hash)) {
        ecs_map_insert(&m->contents, (ecs_map_key_t)hash, (ecs_map_val_t)slot);
        s->flags |= BOX_MATERIALS_LISTED;
    }
}

/** Take a slot out of the lookup by content.
 *
 * @param m The table
 * @param slot The slot
 */
static void box_materials_unlist(
    box_draw_materials_t *m,
    uint32_t slot)
{
    box_draw_material_slot_t *s = box_materials_slot(m, slot);
    if (s->flags & BOX_MATERIALS_LISTED) {
        ecs_map_remove(&m->contents, (ecs_map_key_t)s->hash);
        s->flags &= (uint8_t)~BOX_MATERIALS_LISTED;
    }
}

/** Hand out a slot: the last freed slot, else the one after the highest.
 *
 * @param m The table
 * @return The slot, without owners and packed in the next send
 */
static uint32_t box_materials_alloc(
    box_draw_materials_t *m)
{
    uint32_t result;
    if (ecs_vec_count(&m->free)) {
        result = *ecs_vec_last_t(&m->free, uint32_t);
        ecs_vec_remove_last(&m->free);
    } else {
        result = (uint32_t)ecs_vec_count(&m->records);
        ecs_os_memset_t(ecs_vec_append_t(NULL, &m->records, box_render_material_t), 0,
            box_render_material_t);
        ecs_os_memset_t(ecs_vec_append_t(NULL, &m->slots, box_draw_material_slot_t), 0,
            box_draw_material_slot_t);
    }

    box_draw_material_slot_t *s = box_materials_slot(m, result);
    s->owner = 0;
    s->refs = 0;
    s->flags &= BOX_MATERIALS_LISTS;
    m->alive ++;
    box_materials_mark(m, result);

    return result;
}

/** Put a slot on the free list.
 *
 * @param m The table
 * @param slot The slot
 */
static void box_materials_free(
    box_draw_materials_t *m,
    uint32_t slot)
{
    box_materials_unlist(m, slot);
    box_draw_material_slot_t *s = box_materials_slot(m, slot);
    s->owner = 0;
    s->refs = 0;
    s->flags &= BOX_MATERIALS_LISTS;
    *ecs_vec_append_t(NULL, &m->free, uint32_t) = slot;
    m->alive --;
}

/** Release the use of a slot by one owner: a private slot is freed, a shared slot with its last
 * owner. Slot 0 is never freed.
 *
 * @param m The table
 * @param slot The slot
 */
static void box_materials_drop(
    box_draw_materials_t *m,
    uint32_t slot)
{
    box_draw_material_slot_t *s = box_materials_slot(m, slot);
    if (s->owner) {
        box_materials_free(m, slot);
    } else if (s->refs > 0) {
        s->refs --;
        if (!s->refs && slot != BOX_MATERIALS_DEFAULT) {
            box_materials_free(m, slot);
        }
    }
}

/** Pack the record of an owner from the material components it owns or inherits.
 *
 * @param world The world
 * @param owner The owner, 0 for the default material
 * @param out The record
 */
static void box_materials_pack(
    const ecs_world_t *world,
    ecs_entity_t owner,
    box_render_material_t *out)
{
    box_draw_material_t material;
    box_draws_material_of(world, owner, &material);
    box_draws_material_pack(out, &material);
}

/** Record the shadow regions of the alpha tested casters that render with a slot whose cutoff,
 * UV transform or texture mapping changed, while shadow regions are recorded.
 *
 * @param d The state
 * @param slot The slot
 * @param was Its record before
 * @param record Its record now
 */
static void box_materials_shadow(
    box_render_draws_t *d,
    uint32_t slot,
    const box_render_material_t *was,
    const box_render_material_t *record)
{
    if (d->shadow_record && (was->cutoff != record->cutoff ||
        ecs_os_memcmp(was->uv, record->uv, ECS_SIZEOF(float) * 4) ||
        was->mapping != record->mapping || was->mapping_size != record->mapping_size))
    {
        *ecs_vec_append_t(NULL, &d->shadow_materials, uint32_t) = slot;
    }
}

/** Hash the material of an owner and give it the shared record of that content.
 *
 * The owner takes the listed slot of its content, else the slot it had when it held it alone
 * (shared or private), packed again in place, else a new slot. It releases the slot it had when
 * it moves.
 *
 * @param world The world
 * @param d The state
 * @param e The owner
 * @param old Its slot, BOX_MATERIALS_NONE for none
 * @return The shared slot
 */
static uint32_t box_materials_share(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint32_t old)
{
    box_draw_materials_t *m = &d->materials;
    box_render_material_t record;
    box_materials_pack(world, e, &record);
    uint64_t hash = box_materials_hash(&record);
    uint32_t result = box_materials_find(m, hash, &record);
    if (result != BOX_MATERIALS_NONE) {
        if (result != old) {
            if (old != BOX_MATERIALS_NONE) {
                box_materials_drop(m, old);
            }

            box_materials_slot(m, result)->refs ++;
        }

        goto done;
    }

    if (old != BOX_MATERIALS_NONE && old != BOX_MATERIALS_DEFAULT) {
        box_draw_material_slot_t *s = box_materials_slot(m, old);
        if (s->owner == e || (!s->owner && s->refs == 1)) {
            box_materials_unlist(m, old);
            s->owner = 0;
            s->refs = 1;
            s->flags &= BOX_MATERIALS_LISTS;
            box_materials_shadow(d, old, box_materials_record(m, old), &record);
            *box_materials_record(m, old) = record;
            box_materials_list(m, old, hash);
            box_materials_mark(m, old);
            result = old;
            goto done;
        }
    }

    if (old != BOX_MATERIALS_NONE) {
        box_materials_drop(m, old);
    }

    result = box_materials_alloc(m);
    *box_materials_record(m, result) = record;
    box_materials_slot(m, result)->refs = 1;
    box_materials_list(m, result, hash);
done:
    return result;
}

/** Give an owner a private record.
 *
 * A shared slot the owner holds alone turns private in place; else the owner releases its slot
 * and gets a new one. The slot is packed from the owner in the next send.
 *
 * @param m The table
 * @param e The owner
 * @param old Its slot, BOX_MATERIALS_NONE for none
 * @return The private slot
 */
static uint32_t box_materials_private(
    box_draw_materials_t *m,
    ecs_entity_t e,
    uint32_t old)
{
    uint32_t result;
    if (old != BOX_MATERIALS_NONE) {
        box_draw_material_slot_t *s = box_materials_slot(m, old);
        if (s->owner == e) {
            result = old;
            goto done;
        }

        if (old != BOX_MATERIALS_DEFAULT && s->refs == 1) {
            box_materials_unlist(m, old);
            s->refs = 0;
            s->owner = e;
            s->flags &= BOX_MATERIALS_LISTS;
            box_materials_mark(m, old);
            result = old;
            goto done;
        }

        box_materials_drop(m, old);
    }

    result = box_materials_alloc(m);
    box_materials_slot(m, result)->owner = e;
done:
    return result;
}

/** Store the slot of an owner, which clears its pending change, and set its MaterialId. The IsA
 * instances that inherit the MaterialId of an owner whose slot changed write their shading again.
 *
 * @param world The world
 * @param d The state
 * @param e The owner
 * @param slot Its slot
 * @param mark Whether the owner writes its shading again
 */
static void box_materials_set(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    uint32_t slot,
    bool mark)
{
    *ecs_map_ensure(&d->materials.owned, (ecs_map_key_t)e) = (ecs_map_val_t)slot;
    MaterialId *id = ecs_get_mut(world, e, MaterialId);
    if (id && id->value != slot) {
        id->value = slot;
        if (ecs_id_in_use(world, ecs_pair(EcsIsA, e))) {
            ecs_modified(world, e, MaterialId);
        }
    }

    if (mark) {
        box_draws_mark(world, d, e, BOX_DRAW_SHADING);
    }
}

/** Put the slot of an owner on the free list, or release its share of it.
 *
 * @param m The table
 * @param owner The owner
 * @return Whether the owner had a slot
 */
static bool box_materials_give(
    box_draw_materials_t *m,
    ecs_entity_t owner)
{
    ecs_map_val_t *slot = ecs_map_get(&m->owned, (ecs_map_key_t)owner);
    bool result = slot != NULL;
    if (!result) {
        goto done;
    }

    box_materials_drop(m, (uint32_t)*slot);
    ecs_map_remove(&m->owned, (ecs_map_key_t)owner);
done:
    return result;
}

void box_draws_materials_init(
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    ecs_os_memset_t(m, 0, box_draw_materials_t);
    ecs_vec_init_t(NULL, &m->records, box_render_material_t, 0);
    ecs_vec_init_t(NULL, &m->slots, box_draw_material_slot_t, 0);
    ecs_vec_init_t(NULL, &m->dirty, uint32_t, 0);
    ecs_vec_init_t(NULL, &m->free, uint32_t, 0);
    ecs_vec_init_t(NULL, &m->pending, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &m->transiting, uint32_t, 0);
    ecs_vec_init_t(NULL, &m->uploaded, box_draw_material_upload_t, 0);
    ecs_map_init(&m->owned, NULL);
    ecs_map_init(&m->contents, NULL);
    m->uploaded_frame = -1;
    box_render_material_t *record = ecs_vec_append_t(NULL, &m->records, box_render_material_t);
    box_draws_material_pack(record, &(box_draw_material_t){ .fade = 1.0f });
    box_draw_material_slot_t *s = ecs_vec_append_t(NULL, &m->slots, box_draw_material_slot_t);
    ecs_os_memset_t(s, 0, box_draw_material_slot_t);
    s->refs = 1;
    box_materials_list(m, BOX_MATERIALS_DEFAULT, box_materials_hash(record));
    m->alive = 1;
    box_materials_mark(m, BOX_MATERIALS_DEFAULT);
}

void box_draws_materials_fini(
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    ecs_vec_fini_t(NULL, &m->records, box_render_material_t);
    ecs_vec_fini_t(NULL, &m->slots, box_draw_material_slot_t);
    ecs_vec_fini_t(NULL, &m->dirty, uint32_t);
    ecs_vec_fini_t(NULL, &m->free, uint32_t);
    ecs_vec_fini_t(NULL, &m->pending, ecs_entity_t);
    ecs_vec_fini_t(NULL, &m->transiting, uint32_t);
    ecs_vec_fini_t(NULL, &m->uploaded, box_draw_material_upload_t);
    ecs_map_fini(&m->owned);
    ecs_map_fini(&m->contents);
}

void box_draws_materials_reset(
    box_render_draws_t *d)
{
    d->materials.capacity = 0;
}

bool box_draws_material_changed(
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_materials_t *m = &d->materials;
    ecs_map_val_t *own = ecs_map_get(&m->owned, (ecs_map_key_t)e);
    if (!own) {
        goto done;
    }

    uint32_t slot = (uint32_t)*own;
    if (box_materials_slot(m, slot)->owner == e) {
        box_materials_mark(m, slot);
    } else if (!(*own & BOX_MATERIALS_PENDING)) {
        *own |= BOX_MATERIALS_PENDING;
        *ecs_vec_append_t(NULL, &m->pending, ecs_entity_t) = e;
    }

done:
    return own != NULL;
}

bool box_draws_material_owns(
    const box_render_draws_t *d,
    ecs_entity_t e)
{
    return ecs_map_get(&d->materials.owned, (ecs_map_key_t)e) != NULL;
}

uint32_t box_draws_material_private(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_materials_t *m = &d->materials;
    ecs_map_val_t *own = ecs_map_get(&m->owned, (ecs_map_key_t)e);
    uint32_t result = BOX_MATERIALS_NONE;
    if (!own) {
        goto done;
    }

    uint32_t old = (uint32_t)*own;
    result = box_materials_private(m, e, old);
    if (result != old || (*own & BOX_MATERIALS_PENDING)) {
        box_materials_set(world, d, e, result, result != old);
    }

    box_draw_material_slot_t *s = box_materials_slot(m, result);
    s->flags |= BOX_MATERIALS_TRANSITION;
    s->stamp = m->stamp;
    if (!(s->flags & BOX_MATERIALS_TRANSITING)) {
        s->flags |= BOX_MATERIALS_TRANSITING;
        *ecs_vec_append_t(NULL, &m->transiting, uint32_t) = result;
    }

done:
    return result;
}

/** End the material transitions that the walk of this frame did not take: their owners return
 * to a shared record.
 *
 * A transition starts when the walk first takes the owner with box_draws_material_private: a
 * shared slot it holds alone turns private in place, else it gets a new slot and its sharers keep
 * theirs. Each frame the walk takes the slot it is stamped, and the walk uploads it again after.
 *
 * At the end the owner takes the shared slot of its content and frees the private one, or its
 * slot turns shared in place. A prefab or a shape of a DynamicMaterial batch stays private.
 *
 * @param world The world
 * @param d The state
 */
static void box_materials_transitions_end(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    int32_t i, kept = 0;
    for (i = 0; i < ecs_vec_count(&m->transiting); i ++) {
        uint32_t slot = *ecs_vec_get_t(&m->transiting, uint32_t, i);
        box_draw_material_slot_t *s = box_materials_slot(m, slot);
        ecs_entity_t e = s->owner;
        if (e && (s->flags & BOX_MATERIALS_TRANSITION) && s->stamp == m->stamp) {
            *ecs_vec_get_t(&m->transiting, uint32_t, kept ++) = slot;
            continue;
        }

        s->flags &= (uint8_t)~(BOX_MATERIALS_TRANSITION | BOX_MATERIALS_TRANSITING);
        if (e && !(s->flags & BOX_MATERIALS_DYNAMIC) && ecs_is_alive(world, e) &&
            !ecs_has_id(world, e, EcsPrefab))
        {
            uint32_t shared = box_materials_share(world, d, e, slot);
            box_materials_set(world, d, e, shared, shared != slot);
        }
    }

    ecs_vec_set_count_t(NULL, &m->transiting, uint32_t, kept);
}

void box_draws_materials_resolve(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    int32_t i;
    box_materials_transitions_end(world, d);
    for (i = 0; i < ecs_vec_count(&m->pending); i ++) {
        ecs_entity_t e = *ecs_vec_get_t(&m->pending, ecs_entity_t, i);
        ecs_map_val_t *own = ecs_map_get(&m->owned, (ecs_map_key_t)e);
        if (!own || !(*own & BOX_MATERIALS_PENDING)) {
            continue;
        }

        uint32_t slot = (uint32_t)*own;
        *own = (ecs_map_val_t)slot;
        if (box_materials_slot(m, slot)->owner == e || !ecs_is_alive(world, e)) {
            continue;
        }

        uint32_t shared = box_materials_share(world, d, e, slot);
        if (shared != slot) {
            box_materials_set(world, d, e, shared, true);
        }
    }

    ecs_vec_clear(&m->pending);
    m->stamp ++;
}

void box_draws_material_release(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    if (!ecs_map_get(&d->materials.owned, (ecs_map_key_t)e) ||
        ecs_has_id(world, e, EcsPrefab) || !box_materials_give(&d->materials, e))
    {
        goto done;
    }

    MaterialId *id = ecs_get_mut(world, e, MaterialId);
    if (id) {
        id->value = UINT32_MAX;
    }

done:
    return;
}

uint32_t box_draws_material_id(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    bool mesh,
    bool dynamic)
{
    box_draw_materials_t *m = &d->materials;
    ecs_map_val_t *own = ecs_map_get(&m->owned, (ecs_map_key_t)e);
    uint32_t result = BOX_MATERIALS_DEFAULT;
    if (mesh && ecs_owns(world, e, MaterialId)) {
        uint32_t old = own ? (uint32_t)*own : BOX_MATERIALS_NONE;
        bool pending = own && (*own & BOX_MATERIALS_PENDING);
        box_draw_material_slot_t *s = own ? box_materials_slot(m, old) : NULL;
        if (dynamic) {
            result = box_materials_private(m, e, old);
            box_materials_slot(m, result)->flags |= BOX_MATERIALS_DYNAMIC;
        } else if (s && s->owner == e) {
            s->flags &= (uint8_t)~BOX_MATERIALS_DYNAMIC;
            result = (s->flags & BOX_MATERIALS_TRANSITION)
                ? old : box_materials_share(world, d, e, old);
        } else if (!own || pending) {
            result = box_materials_share(world, d, e, old);
        } else {
            result = old;
        }

        if (result != old || pending) {
            box_materials_set(world, d, e, result, false);
        }

        goto check;
    }

    if (own) {
        box_draws_material_release(world, d, e);
    }

    const MaterialId *id = ecs_get(world, e, MaterialId);
    if (id && id->value != UINT32_MAX) {
        result = id->value;
    }

check:
    if (result >= BOX_RENDER_MATERIALS_MAX) {
        if (!m->overflow) {
            ecs_err("material id %u is past the %u records the material table binds, "
                "rendering with the default material", result, BOX_RENDER_MATERIALS_MAX);
            m->overflow = true;
        }

        result = BOX_MATERIALS_DEFAULT;
    }

    return result;
}

/** Hand a prefab that got its MaterialId a private slot, or keep the slot an entity that became
 * a prefab already had, which turns private. An entity that shared its slot with other owners
 * gets a new one, which its IsA instances learn from the OnSet of MaterialId.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnPrefab(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    box_draw_materials_t *m = &d->materials;
    MaterialId *ids = ecs_field(it, MaterialId, 0);
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        ecs_map_val_t *slot = ecs_map_get(&m->owned, (ecs_map_key_t)it->entities[i]);
        uint32_t result = box_materials_private(m, it->entities[i],
            slot ? (uint32_t)*slot : BOX_MATERIALS_NONE);
        *ecs_map_ensure(&m->owned, (ecs_map_key_t)it->entities[i]) = (ecs_map_val_t)result;
        bool changed = ids[i].value != result;
        ids[i].value = result;
        box_materials_mark(m, result);
        if (changed && ecs_id_in_use(it->world, ecs_pair(EcsIsA, it->entities[i]))) {
            ecs_modified(it->world, it->entities[i], MaterialId);
        }
    }
}

/** Forget the value an added MaterialId of an entity that is not a prefab was seeded with, the
 * inherited id or that of a clone, and let the extract decide whether it gets a slot: a mesh
 * shape does, a light, a 2D shape or a text does not.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnOwn(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    box_draw_materials_t *m = &d->materials;
    MaterialId *ids = ecs_field(it, MaterialId, 0);
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        ecs_map_val_t *slot = ecs_map_get(&m->owned, (ecs_map_key_t)it->entities[i]);
        ids[i].value = slot ? (uint32_t)*slot : UINT32_MAX;
        box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_SHADING);
    }
}

/** Free the private slot of an entity that loses its MaterialId or is deleted, prefabs included,
 * or release its share of a shared slot; a shape that stays renders with the id it inherits
 * again.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnRemove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_materials_give(&d->materials, it->entities[i]);
        box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_SHADING);
    }
}

/** Write the shading of the shapes whose inherited MaterialId changed again: the base got a
 * slot, lost it or the shape got another base.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnInherited(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_SHADING);
    }
}

/** Pack the record of an entity that got or lost an IsA base again, from what it inherits now,
 * and write its shading again, whose id may come from the new base.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnIsA(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        box_draws_material_changed(d, it->entities[i]);
        box_draws_mark(it->world, d, it->entities[i], BOX_DRAW_SHADING);
    }
}

/** Follow a set or removal of a material component on its owner, or on a base that propagates
 * it to the entities that inherit it, which the default source of the term matches.
 *
 * Owners of a private slot pack it again, owners of a shared slot hash their material in the
 * next extract; flecs does not propagate past an owner of the component, so an owner of the
 * changed field is left alone. 2D shapes and texts write their shading again.
 *
 * Mesh shapes reference the slot and write nothing, unless their batch changes (an added or
 * removed AlphaCutoff or Transmission) or the owner of a shared slot moves to another slot.
 *
 * @param it The iterator of the observer
 */
static void BoxMaterialsOnComponent(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    ecs_world_t *world = it->world;
    const ecs_table_t *table = it->table;
    int32_t i;
    if (ecs_is_fini(world) || !table) {
        goto done;
    }

    bool owners = ecs_table_has_id(world, table, ecs_id(MaterialId));
    bool prefab = ecs_table_has_id(world, table, EcsPrefab);
    bool inline_ = !ecs_table_has_id(world, table, ecs_id(ShapeMesh)) ||
        ecs_table_has_id(world, table, ecs_id(Shape2D)) ||
        ecs_table_has_id(world, table, ecs_id(Text));
    bool keyed = (it->event_id == ecs_id(AlphaCutoff) || it->event_id == ecs_id(Transmission)) &&
        it->event != EcsOnSet;
    bool mark = !prefab && (inline_ || keyed);
    for (i = 0; i < it->count; i ++) {
        if (owners) {
            box_draws_material_changed(d, it->entities[i]);
        }

        if (mark) {
            box_draws_mark(world, d, it->entities[i], BOX_DRAW_SHADING);
        }
    }

done:
    return;
}

void box_draws_materials_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_entity_t components[11] = {
        ecs_id(Rgb), ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(TextureTransform),
        ecs_id(AlphaCutoff), ecs_id(Transmission), ecs_id(ClearCoat), ecs_id(SpecularAnisotropy),
        ecs_id(Sheen), ecs_id(DiffuseTransmission), ecs_id(TextureMapping)
    };
    const char *names[11] = {
        "DrawsOnColor", "DrawsOnPbrMaterial", "DrawsOnEmissive", "DrawsOnTextureTransform",
        "DrawsOnAlphaCutoff", "DrawsOnTransmission", "DrawsOnClearCoat",
        "DrawsOnSpecularAnisotropy", "DrawsOnSheen", "DrawsOnDiffuseTransmission",
        "DrawsOnTextureMapping"
    };
    int32_t i;
    box_draws_observer(world, d, "MaterialsOnPrefab", (ecs_observer_desc_t){
        .query.terms = {
            { .id = ecs_id(MaterialId), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .events = { EcsOnAdd },
        .yield_existing = true }, BoxMaterialsOnPrefab);
    box_draws_observer(world, d, "MaterialsOnOwn", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(MaterialId), .src.id = EcsSelf }},
        .events = { EcsOnAdd } }, BoxMaterialsOnOwn);
    box_draws_observer(world, d, "MaterialsOnRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(MaterialId), .src.id = EcsSelf }},
        .query.flags = EcsQueryMatchPrefab,
        .events = { EcsOnRemove } }, BoxMaterialsOnRemove);
    box_draws_observer(world, d, "MaterialsOnInherited", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(MaterialId), .src.id = EcsUp, .trav = EcsIsA }},
        .events = { EcsOnAdd, EcsOnRemove, EcsOnSet } }, BoxMaterialsOnInherited);
    box_draws_observer(world, d, "MaterialsOnIsA", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_pair(EcsIsA, EcsWildcard) }},
        .query.flags = EcsQueryMatchPrefab,
        .events = { EcsOnAdd, EcsOnRemove } }, BoxMaterialsOnIsA);

    for (i = 0; i < 11; i ++) {
        bool keyed = components[i] == ecs_id(AlphaCutoff) ||
            components[i] == ecs_id(Transmission);
        box_draws_observer(world, d, names[i], (ecs_observer_desc_t){
            .query.terms = {{
                .id = components[i],
                .src.id = EcsSelf | EcsUp,
                .trav = EcsIsA
            }},
            .query.flags = EcsQueryMatchPrefab,
            .events = { EcsOnSet, EcsOnRemove, keyed ? EcsOnAdd : 0 } },
            BoxMaterialsOnComponent);
    }
}

void box_draws_material_of(
    const ecs_world_t *world,
    ecs_entity_t owner,
    box_draw_material_t *out)
{
    *out = (box_draw_material_t){ .fade = 1.0f };
    if (owner) {
        out->color = ecs_get(world, owner, Rgb);
        out->pbr = ecs_get(world, owner, PbrMaterial);
        out->emissive = ecs_get(world, owner, Emissive);
        out->uv = ecs_get(world, owner, TextureTransform);
        out->mapping = ecs_get(world, owner, TextureMapping);
        out->cutoff = ecs_get(world, owner, AlphaCutoff);
        out->transmission = ecs_get(world, owner, Transmission);
        out->clear_coat = ecs_get(world, owner, ClearCoat);
        out->anisotropy = ecs_get(world, owner, SpecularAnisotropy);
        out->sheen = ecs_get(world, owner, Sheen);
        out->diffuse_transmission = ecs_get(world, owner, DiffuseTransmission);
    }
}

bool box_draws_material_data(
    const box_render_draws_t *d,
    uint32_t slot,
    int64_t frame,
    uint32_t *out)
{
    const box_draw_materials_t *m = &d->materials;
    int32_t i, count = m->uploaded_frame == frame ? ecs_vec_count(&m->uploaded) : 0;
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        const box_draw_material_upload_t *u = ecs_vec_get_t(
            &m->uploaded, box_draw_material_upload_t, i);
        if (slot >= u->first && slot - u->first < u->count) {
            *out = u->data + (slot - u->first) * (uint32_t)sizeof(box_render_material_t);
            result = true;
        }
    }

    return result;
}

/** Order slots ascending.
 *
 * @param a The first slot
 * @param b The second slot
 * @return Negative when a comes first
 */
static int box_materials_cmp(
    const void *a,
    const void *b)
{
    uint32_t sa = *(const uint32_t*)a, sb = *(const uint32_t*)b;

    return (sa > sb) - (sa < sb);
}

/** Upload consecutive records of the table.
 *
 * @param d The state
 * @param p The packet
 * @param first First slot
 * @param count Slots
 */
static void box_materials_upload(
    box_render_draws_t *d,
    box_render_packet_t *p,
    uint32_t first,
    uint32_t count)
{
    uint32_t size = count * (uint32_t)sizeof(box_render_material_t);
    void *dst = box_render_packet_upload(p, BOX_RENDER_BUFFER_MATERIALS,
        first * (uint32_t)sizeof(box_render_material_t), size);
    ecs_os_memcpy(dst, ecs_vec_get_t(&d->materials.records, box_render_material_t,
        (int32_t)first), (ecs_size_t)size);
    d->stats.material_bytes += size;
    *ecs_vec_append_t(NULL, &d->materials.uploaded, box_draw_material_upload_t) =
        (box_draw_material_upload_t){
            .first = first,
            .count = count,
            .data = ecs_vec_last_t(&p->uploads, box_render_upload_t)->data
        };
}

void box_draws_materials_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    box_draw_materials_t *m = &d->materials;
    int32_t i, j, count = ecs_vec_count(&m->dirty);
    uint32_t *dirty = ecs_vec_first_t(&m->dirty, uint32_t);
    uint32_t total = (uint32_t)ecs_vec_count(&m->records);
    ecs_vec_clear(&m->uploaded);
    m->uploaded_frame = p->frame;
    for (i = 0, j = 0; i < count; i ++) {
        uint32_t slot = dirty[i];
        box_draw_material_slot_t *s = box_materials_slot(m, slot);
        ecs_entity_t owner = s->owner;
        s->flags &= (uint8_t)~BOX_MATERIALS_MARKED;
        if (!owner && !s->refs) {
            continue;
        }

        dirty[j ++] = slot;
        if (owner && ecs_is_alive(world, owner)) {
            box_render_material_t *record = box_materials_record(m, slot);
            box_render_material_t was = *record;
            box_materials_pack(world, owner, record);
            box_materials_shadow(d, slot, &was, record);
        }
    }

    count = j;

    if (count || !m->features_valid) {
        const box_render_material_t *records = ecs_vec_first_t(&m->records,
            box_render_material_t);
        m->features = 0;
        m->features_valid = true;
        for (i = 0; i < (int32_t)total; i ++) {
            m->features |= records[i].sheen ? BOX_RENDER_MATERIAL_SHEEN : 0u;
            m->features |= records[i].anisotropy ? BOX_RENDER_MATERIAL_ANISOTROPY : 0u;
            m->features |= records[i].clear_coat ? BOX_RENDER_MATERIAL_CLEAR_COAT : 0u;
        }
    }

    p->material_features = m->features;

    if (!m->capacity || total > m->capacity) {
        uint32_t capacity = m->capacity ? m->capacity : BOX_RENDER_MATERIALS_MIN;
        while (capacity < total && capacity < BOX_RENDER_MATERIALS_MAX) {
            capacity *= 2;
        }

        if (capacity > BOX_RENDER_MATERIALS_MAX) {
            capacity = BOX_RENDER_MATERIALS_MAX;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_MATERIALS, sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)capacity * sizeof(box_render_material_t);
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        m->capacity = capacity;
        box_materials_upload(d, p, 0, total < capacity ? total : capacity);
        goto done;
    }

    if (count > 1) {
        qsort(dirty, (size_t)count, sizeof(uint32_t), box_materials_cmp);
    }

    for (i = 0; i < count; i = j) {
        uint32_t first = dirty[i], last = dirty[i];
        for (j = i + 1; j < count && dirty[j] == last + 1; j ++) {
            last = dirty[j];
        }

        if (last >= m->capacity) {
            last = m->capacity - 1;
        }

        if (first <= last) {
            box_materials_upload(d, p, first, last - first + 1);
        }
    }

done:
    ecs_vec_clear(&m->dirty);
}
