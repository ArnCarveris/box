/**
 * @file render/materials.c
 * @brief The material table: one GPU record per prefab or mesh shape that owns its material.
 *
 * A prefab gets a slot when it gets its MaterialId, which its instances inherit; a mesh shape
 * that owns a MaterialId gets a slot of its own in the extract, packed field by field from what
 * it owns and what it still inherits. Slot 0 is the default material. Freed slots are reused.
 *
 * A change of a material component packs the slot of its owner again and, through the events
 * flecs propagates down IsA, every slot that inherits the field from it; the instances that
 * render with a changed slot are not touched.
 */

#include "draw.h"

/** Slot of the default material */
#define BOX_MATERIALS_DEFAULT (0u)

/** Get the owner entity of a slot.
 *
 * @param m The table
 * @param slot The slot
 * @return The owner, 0 for slot 0 and free slots
 */
static ecs_entity_t box_materials_owner(
    box_draw_materials_t *m,
    uint32_t slot)
{
    return *ecs_vec_get_t(&m->owners, ecs_entity_t, (int32_t)slot);
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
    bool *marked = ecs_vec_get_t(&m->marked, bool, (int32_t)slot);
    if (!*marked) {
        *marked = true;
        *ecs_vec_append_t(NULL, &m->dirty, uint32_t) = slot;
    }
}

/** Hand out a slot to an owner: the last freed slot, else the one after the highest.
 *
 * @param m The table
 * @param owner The owner
 * @return The slot, packed in the next send
 */
static uint32_t box_materials_take(
    box_draw_materials_t *m,
    ecs_entity_t owner)
{
    uint32_t result;
    if (ecs_vec_count(&m->free)) {
        result = *ecs_vec_last_t(&m->free, uint32_t);
        ecs_vec_remove_last(&m->free);
    } else {
        result = (uint32_t)ecs_vec_count(&m->records);
        ecs_os_memset_t(ecs_vec_append_t(NULL, &m->records, box_render_material_t), 0,
            box_render_material_t);
        *ecs_vec_append_t(NULL, &m->owners, ecs_entity_t) = 0;
        *ecs_vec_append_t(NULL, &m->marked, bool) = false;
    }

    *ecs_vec_get_t(&m->owners, ecs_entity_t, (int32_t)result) = owner;
    ecs_map_insert(&m->owned, (ecs_map_key_t)owner, (ecs_map_val_t)result);
    m->alive ++;
    box_materials_mark(m, result);

    return result;
}

/** Put the slot of an owner on the free list.
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

    *ecs_vec_get_t(&m->owners, ecs_entity_t, (int32_t)*slot) = 0;
    *ecs_vec_append_t(NULL, &m->free, uint32_t) = (uint32_t)*slot;
    ecs_map_remove(&m->owned, (ecs_map_key_t)owner);
    m->alive --;
done:
    return result;
}

void box_draws_materials_init(
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    ecs_os_memset_t(m, 0, box_draw_materials_t);
    ecs_vec_init_t(NULL, &m->records, box_render_material_t, 0);
    ecs_vec_init_t(NULL, &m->owners, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &m->marked, bool, 0);
    ecs_vec_init_t(NULL, &m->dirty, uint32_t, 0);
    ecs_vec_init_t(NULL, &m->free, uint32_t, 0);
    ecs_vec_init_t(NULL, &m->uploaded, box_draw_material_upload_t, 0);
    ecs_map_init(&m->owned, NULL);
    m->uploaded_frame = -1;
    ecs_os_memset_t(ecs_vec_append_t(NULL, &m->records, box_render_material_t), 0,
        box_render_material_t);
    *ecs_vec_append_t(NULL, &m->owners, ecs_entity_t) = 0;
    *ecs_vec_append_t(NULL, &m->marked, bool) = false;
    m->alive = 1;
    box_materials_mark(m, BOX_MATERIALS_DEFAULT);
}

void box_draws_materials_fini(
    box_render_draws_t *d)
{
    box_draw_materials_t *m = &d->materials;
    ecs_vec_fini_t(NULL, &m->records, box_render_material_t);
    ecs_vec_fini_t(NULL, &m->owners, ecs_entity_t);
    ecs_vec_fini_t(NULL, &m->marked, bool);
    ecs_vec_fini_t(NULL, &m->dirty, uint32_t);
    ecs_vec_fini_t(NULL, &m->free, uint32_t);
    ecs_vec_fini_t(NULL, &m->uploaded, box_draw_material_upload_t);
    ecs_map_fini(&m->owned);
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
    ecs_map_val_t *slot = ecs_map_get(&m->owned, (ecs_map_key_t)e);
    if (slot) {
        box_materials_mark(m, (uint32_t)*slot);
    }

    return slot != NULL;
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
    bool mesh)
{
    box_draw_materials_t *m = &d->materials;
    ecs_map_val_t *own = ecs_map_get(&m->owned, (ecs_map_key_t)e);
    uint32_t result = BOX_MATERIALS_DEFAULT;
    if (mesh && ecs_owns(world, e, MaterialId)) {
        if (own) {
            result = (uint32_t)*own;
            goto check;
        }

        result = box_materials_take(m, e);
        MaterialId *id = ecs_get_mut(world, e, MaterialId);
        if (id) {
            id->value = result;
        }

        if (ecs_id_in_use(world, ecs_pair(EcsIsA, e))) {
            ecs_modified(world, e, MaterialId);
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

/** Hand a prefab that got its MaterialId a slot of its own, or keep the slot an entity that
 * became a prefab already had.
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
        if (slot) {
            ids[i].value = (uint32_t)*slot;
            box_materials_mark(m, (uint32_t)*slot);
        } else {
            ids[i].value = box_materials_take(m, it->entities[i]);
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

/** Free the slot of an entity that loses its MaterialId or is deleted, prefabs included; a
 * shape that stays renders with the id it inherits again.
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

/** Pack the slot of an entity that got or lost an IsA base again, from what it inherits now, and
 * write its shading again, whose id may come from the new base.
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
 * Owners of a slot pack it again; flecs does not propagate past an owner of the component, so a
 * slot that owns the changed field is left alone. 2D shapes and texts write their shading again.
 *
 * Mesh shapes reference the slot and write nothing, unless an added or removed AlphaCutoff or
 * Transmission changes their batch.
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
    ecs_entity_t components[6] = {
        ecs_id(Rgb), ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(TextureTransform),
        ecs_id(AlphaCutoff), ecs_id(Transmission)
    };
    const char *names[6] = {
        "DrawsOnColor", "DrawsOnPbrMaterial", "DrawsOnEmissive", "DrawsOnTextureTransform",
        "DrawsOnAlphaCutoff", "DrawsOnTransmission"
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

    for (i = 0; i < 6; i ++) {
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
        out->cutoff = ecs_get(world, owner, AlphaCutoff);
        out->transmission = ecs_get(world, owner, Transmission);
    }
}

/** Pack the record of a slot from the material components its owner owns or inherits.
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
    for (i = 0; i < count; i ++) {
        uint32_t slot = dirty[i];
        ecs_entity_t owner = box_materials_owner(m, slot);
        *ecs_vec_get_t(&m->marked, bool, (int32_t)slot) = false;
        if (slot == BOX_MATERIALS_DEFAULT || (owner && ecs_is_alive(world, owner))) {
            box_render_material_t *record = ecs_vec_get_t(
                &m->records, box_render_material_t, (int32_t)slot);
            box_render_material_t was = *record;
            box_materials_pack(world, owner, record);
            if (d->shadow_record && (was.cutoff != record->cutoff ||
                ecs_os_memcmp(was.uv, record->uv, ECS_SIZEOF(float) * 4)))
            {
                *ecs_vec_append_t(NULL, &d->shadow_materials, uint32_t) = slot;
            }
        }
    }

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
