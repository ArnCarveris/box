/**
 * @file render/movers.c
 * @brief Movers: the entities that own DynamicTransform, which place the mover batches.
 *
 * A shape that inherits DynamicTransform through DynamicTransformSpread keeps its transform
 * relative to its mover in a mover page, written when that transform changes; the shape of a
 * WorldCell root rides itself, its transform relative to itself the scale of its shape.
 *
 * The mover table holds the world transform of every mover with slots relative to the render
 * origin (box_render_frame_t.delta) in f64, sent every frame a mover or the render origin moved;
 * the compose pass places the slots with it, so following the origin costs a record per mover.
 */

#include "draw.h"
#include "../world/world.h"
#include <math.h>

/** Records the mover table on the GPU holds at least, doubling from there */
#define BOX_DRAWS_MOVERS_MIN (64)

/** Relative change of the uniform scale of a mover past which its slots are written again */
#define BOX_DRAWS_MOVER_SCALE_EPSILON (1e-4f)

/** Check whether an entity is a mover: it owns DynamicTransform, or has it without inheriting it
 * from DynamicTransformSpread.
 *
 * @param world The world
 * @param e The entity
 * @return Whether it is a mover
 */
static bool box_draws_is_mover(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_owns_id(world, e, DynamicTransform) ||
        (ecs_has_id(world, e, DynamicTransform) &&
            !ecs_has_pair(world, e, EcsIsA, DynamicTransformSpread));
}

ecs_entity_t box_draws_mover_of(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t result = 0, cur = e ? ecs_get_parent(world, e) : 0;
    for (; cur && !result; cur = ecs_get_parent(world, cur)) {
        if (box_draws_is_mover(world, cur)) {
            result = cur;
        }
    }

    return result;
}

ecs_entity_t box_draws_mover_at(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    return e && box_draws_is_mover(world, e) ? e : box_draws_mover_of(world, e);
}

void box_draws_movers_init(
    box_render_draws_t *d)
{
    ecs_vec_init_t(NULL, &d->movers, box_draw_mover_t, 0);
    box_draw_mover_t *identity = ecs_vec_append_t(NULL, &d->movers, box_draw_mover_t);
    ecs_os_memset_t(identity, 0, box_draw_mover_t);
    identity->pose.rows[0][0] = 1.0f;
    identity->pose.rows[1][1] = 1.0f;
    identity->pose.rows[2][2] = 1.0f;
    identity->was = identity->pose;
    identity->scale = 1.0f;
    ecs_map_init(&d->mover_of, NULL);
    ecs_map_init(&d->locals, NULL);
    ecs_map_init(&d->frame_poses, NULL);
    ecs_vec_init_t(NULL, &d->frame_cache, box_draw_frame_pose_t, 0);
    d->free_mover = -1;
    d->movers_size = 0;
}

void box_draws_movers_fini(
    box_render_draws_t *d)
{
    ecs_vec_fini_t(NULL, &d->movers, box_draw_mover_t);
    ecs_map_fini(&d->mover_of);
    ecs_map_fini(&d->locals);
    ecs_map_fini(&d->frame_poses);
    ecs_vec_fini_t(NULL, &d->frame_cache, box_draw_frame_pose_t);
}

void box_draws_movers_reset(
    box_render_draws_t *d)
{
    d->movers_size = 0;
    d->movers_dirty = true;
}

box_draw_mover_t* box_draws_mover(
    const box_render_draws_t *d,
    uint32_t index)
{
    return ecs_vec_get_t(&d->movers, box_draw_mover_t, (int32_t)index);
}

/** Set the f64 translation of a mover from its pose: the exact corner of the cell of a WorldCell
 * root in the frame of the local origin, else the translation of the pose.
 *
 * @param world The world
 * @param m The mover, its pose set
 * @param frame The frame of the local origin
 */
static void box_draws_mover_exact(
    const ecs_world_t *world,
    box_draw_mover_t *m,
    const box_world_frame_t *frame)
{
    const WorldCell *cell = ecs_is_alive(world, m->entity)
        ? ecs_get(world, m->entity, WorldCell) : NULL;
    int32_t r;
    if (cell) {
        double corner[3] = {
            (double)cell->x * BOX_WORLD_CELL_SIZE,
            (double)cell->y * BOX_WORLD_CELL_SIZE,
            (double)cell->z * BOX_WORLD_CELL_SIZE
        };
        box_world_frame_to_local(frame, corner, m->at);
    } else {
        for (r = 0; r < 3; r ++) {
            m->at[r] = (double)m->pose.rows[r][3];
        }
    }
}

/** Write the world transform of a mover as the rows of a transform.
 *
 * @param out The transform
 * @param t World transform of the mover
 */
static void box_draws_mover_rows(
    box_render_transform_t *out,
    const WorldTransform *t)
{
    int32_t r, c;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            out->rows[r][c] = t->m[c][r];
        }
    }
}

uint32_t box_draws_mover_ref(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t mover)
{
    uint32_t result = 0;
    box_draw_mover_t *m;
    if (!mover) {
        goto done;
    }

    ecs_map_val_t *v = ecs_map_get(&d->mover_of, mover);
    if (v) {
        result = (uint32_t)*v;
        box_draws_mover(d, result)->refs ++;
        goto done;
    }

    if (d->free_mover >= 0) {
        result = (uint32_t)d->free_mover;
        m = box_draws_mover(d, result);
        d->free_mover = m->next;
    } else {
        result = (uint32_t)ecs_vec_count(&d->movers);
        m = ecs_vec_append_t(NULL, &d->movers, box_draw_mover_t);
    }

    ecs_os_memset_t(m, 0, box_draw_mover_t);
    m->entity = mover;
    m->refs = 1;
    m->moved = true;
    m->scale = 1.0f;
    WorldTransform t_buf;
    const WorldTransform *t = box_transform_world(world, mover, &t_buf);
    if (t) {
        box_world_frame_t frame;
        box_world_frame_get(world, &frame);
        box_draws_mover_rows(&m->pose, t);
        box_draws_mover_exact(world, m, &frame);
        m->scale = box_transform_world_scale(t);
    }

    m->was = m->pose;
    ecs_map_insert(&d->mover_of, mover, (ecs_map_val_t)result);
done:
    return result;
}

void box_draws_mover_unref(
    box_render_draws_t *d,
    uint32_t index)
{
    box_draw_mover_t *m;
    if (!index || index >= (uint32_t)ecs_vec_count(&d->movers)) {
        goto done;
    }

    m = box_draws_mover(d, index);
    if (!m->entity || -- m->refs > 0) {
        goto done;
    }

    ecs_map_remove(&d->mover_of, m->entity);
    m->entity = 0;
    m->refs = 0;
    m->next = d->free_mover;
    d->free_mover = (int32_t)index;
done:
    return;
}

void box_draws_movers_send(
    ecs_world_t *world,
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&d->movers);
    box_world_frame_t frame;
    (void)p;
    box_world_frame_get(world, &frame);
    for (i = 1; i < count; i ++) {
        box_draw_mover_t *m = box_draws_mover(d, (uint32_t)i);
        box_render_transform_t pose;
        if (!m->entity) {
            continue;
        }

        m->was = m->pose;
        d->movers_dirty |= m->moved;
        m->moved = false;
        WorldTransform t_buf;
        const WorldTransform *t = ecs_is_alive(world, m->entity)
            ? box_transform_world(world, m->entity, &t_buf) : NULL;
        if (!t) {
            continue;
        }

        box_draws_mover_rows(&pose, t);
        if (ecs_os_memcmp_t(&pose, &m->pose, box_render_transform_t)) {
            m->pose = pose;
            m->moved = true;
            d->movers_dirty = true;
        }

        box_draws_mover_exact(world, m, &frame);

        float scale = box_transform_world_scale(t);
        if (fabsf(scale - m->scale) > BOX_DRAWS_MOVER_SCALE_EPSILON * fabsf(m->scale)) {
            m->scale = scale;
            *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
                .root = m->entity, .kinds = BOX_DRAW_TRANSFORM | BOX_DRAW_FADE
            };
        }
    }
}

/** Write the record of a mover relative to the render origin: its pose with the translation
 * at - delta, computed in f64, so a mover the render origin follows is at exactly 0.
 *
 * @param out The record
 * @param m The mover
 * @param delta The render origin in the frame of the local origin
 */
static void box_draws_mover_record(
    box_render_transform_t *out,
    const box_draw_mover_t *m,
    const double delta[3])
{
    int32_t r;
    *out = m->pose;
    for (r = 0; r < 3; r ++) {
        out->rows[r][3] = (float)(m->at[r] - delta[r]);
    }
}

/** Send the mover table when a mover moved or joined or the render origin moved, creating it
 * larger when it outgrew its buffer. Index 0, the identity, holds minus the render origin, so
 * the slots without a mover land relative to it too.
 *
 * @param d The state
 * @param p The packet
 */
static void box_draws_movers_upload(
    box_render_draws_t *d,
    box_render_packet_t *p)
{
    int32_t i, count = ecs_vec_count(&d->movers);
    const double *delta = p->local.delta;
    if (ecs_os_memcmp(delta, d->movers_delta, sizeof(d->movers_delta))) {
        d->movers_delta[0] = delta[0];
        d->movers_delta[1] = delta[1];
        d->movers_delta[2] = delta[2];
        d->movers_dirty = true;
    }

    if ((uint32_t)count > d->movers_size) {
        uint32_t size = d->movers_size ? d->movers_size : BOX_DRAWS_MOVERS_MIN;
        while (size < (uint32_t)count) {
            size *= 2;
        }

        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_MOVERS, sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)size * sizeof(box_render_transform_t);
        desc->usage = BOX_RENDER_BUFFER_STORAGE;
        d->movers_size = size;
        d->movers_dirty = true;
    }

    if (d->movers_dirty) {
        uint32_t size = (uint32_t)count * (uint32_t)sizeof(box_render_transform_t);
        box_render_transform_t *out = box_render_packet_upload(p, BOX_RENDER_BUFFER_MOVERS, 0,
            size);
        for (i = 0; i < count; i ++) {
            box_draws_mover_record(&out[i], box_draws_mover(d, (uint32_t)i), delta);
        }

        d->stats.upload_bytes += size;
        d->stats.transform_bytes += size;
        d->movers_dirty = false;
    }
}

void box_draws_compose(
    box_render_transform_t *out,
    const box_render_transform_t *pose,
    const box_render_transform_t *local)
{
    int32_t r, c, k;
    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            float sum = c == 3 ? pose->rows[r][3] : 0.0f;
            for (k = 0; k < 3; k ++) {
                sum += pose->rows[r][k] * local->rows[k][c];
            }

            out->rows[r][c] = sum;
        }
    }
}

void box_draws_relative(
    box_render_transform_t *out,
    const WorldTransform *mover,
    const box_render_transform_t *model)
{
    double a[3][3], inv[3][3], t[3], det;
    int32_t r, c, k;
    if (!mover) {
        *out = *model;
        goto done;
    }

    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            a[r][c] = mover->m[c][r];
        }

        t[r] = mover->m[3][r];
    }

    inv[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    inv[0][1] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    inv[0][2] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    inv[1][0] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    inv[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    inv[1][2] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    inv[2][0] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    inv[2][1] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    inv[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    det = a[0][0] * inv[0][0] + a[0][1] * inv[1][0] + a[0][2] * inv[2][0];
    if (!(fabs(det) > 1e-30) || !isfinite(det)) {
        *out = *model;
        goto done;
    }

    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 3; c ++) {
            inv[r][c] /= det;
        }
    }

    for (r = 0; r < 3; r ++) {
        for (c = 0; c < 4; c ++) {
            double sum = 0.0;
            for (k = 0; k < 3; k ++) {
                sum += inv[r][k] * ((double)model->rows[k][c] - (c == 3 ? t[k] : 0.0));
            }

            out->rows[r][c] = (float)sum;
        }
    }

done:
    return;
}

void box_draws_mover_slot(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    const ShapeMesh *shape,
    uint32_t *rec,
    box_render_transform_t *out)
{
    ecs_entity_t mover = box_draws_mover_at(world, e);
    uint32_t index = box_draws_mover_ref(world, d, mover);
    box_draws_mover_unref(d, *rec & BOX_RENDER_MOVER_INDEX);
    *rec = (*rec & BOX_RENDER_MOVER_FRAME) | index;
    if (mover == e) {
        WorldTransform identity;
        glm_mat4_identity(identity.m);
        box_draws_transform(out, &identity, shape);
    } else {
        box_render_transform_t model = *out;
        WorldTransform mover_buf;
        box_draws_relative(out, box_transform_world(world, mover, &mover_buf), &model);
    }
}

uint32_t* box_draws_slot_mover(
    box_draw_batch_t *b,
    int32_t slot)
{
    return b->key.mover ? ecs_vec_get_t(&b->movers, uint32_t, slot) : NULL;
}

const box_render_transform_t* box_draws_slot_world(
    const box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot,
    box_render_transform_t *tmp)
{
    const box_render_transform_t *result = box_draws_slot_transform(b, slot);
    uint32_t *mover = box_draws_slot_mover(b, slot);
    if (mover) {
        box_draws_compose(tmp, &box_draws_mover(d, *mover & BOX_RENDER_MOVER_INDEX)->pose,
            result);
        result = tmp;
    }

    return result;
}

void box_draws_frame_write(
    const ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e,
    box_draw_batch_t *b,
    int32_t slot,
    const box_render_transform_t *model,
    bool lookup)
{
    box_draw_frame_src_t *src = box_draws_slot_frame_src(b, slot);
    box_render_transform_t *out = box_draws_slot_frame(b, slot);
    uint32_t *mover = box_draws_slot_mover(b, slot);
    bool was_live = src->live;
    if (lookup) {
        box_draws_frame_src(world, e, src);
    }

    if (!mover) {
        box_draws_frame_record(world, d, src, out, model);
        goto done;
    }

    const box_draw_frame_pose_t *pose = src->frame
        ? box_draws_frame_pose(world, d, src->frame) : NULL;
    if (src->mode == MappingObject || !pose || !pose->valid) {
        box_draws_frame_record(world, d, src, out, box_draws_slot_transform(b, slot));
        *mover |= BOX_RENDER_MOVER_FRAME;
        goto done;
    }

    ecs_entity_t own = box_draws_mover(d, *mover & BOX_RENDER_MOVER_INDEX)->entity;
    src->live = box_draws_mover_at(world, src->frame) != own;
    box_draws_frame_record(world, d, src, out, model);
    *mover &= ~BOX_RENDER_MOVER_FRAME;
done:
    b->live_frames += (int32_t)src->live - (int32_t)was_live;

    return;
}

/** Hash the local transform of an entity: its Position, Rotation and Scale, owned or inherited,
 * or the WorldTransform a ManualTransform entity has.
 *
 * @param world The world
 * @param e The entity
 * @return The hash, never 0
 */
static uint64_t box_draws_local_hash(
    const ecs_world_t *world,
    ecs_entity_t e)
{
    const void *parts[4] = {
        ecs_get(world, e, Position), ecs_get(world, e, Rotation), ecs_get(world, e, Scale),
        ecs_has_id(world, e, ManualTransform) ? ecs_get(world, e, WorldTransform) : NULL
    };
    const ecs_size_t sizes[4] = {
        ECS_SIZEOF(Position), ECS_SIZEOF(Rotation), ECS_SIZEOF(Scale), ECS_SIZEOF(WorldTransform)
    };
    uint64_t result = 0xCBF29CE484222325ull;
    int32_t i, b;
    for (i = 0; i < 4; i ++) {
        const uint8_t *bytes = parts[i];
        result = (result ^ (uint64_t)(i + 1)) * 0x100000001B3ull;
        for (b = 0; bytes && b < sizes[i]; b ++) {
            result = (result ^ bytes[b]) * 0x100000001B3ull;
        }
    }

    return result ? result : 1;
}

/** Remember the entities under a mover whose local transform changed as roots of subtrees that
 * move in the frame of their mover, which keep their keys.
 *
 * A mover that moves itself does not move its subtree in its frame, and an entity set to the
 * local transform its subtree was last written with, as a template run again, writes nothing.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnLocal(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (box_draws_mover_at(it->world, e) == e) {
            continue;
        }

        uint64_t hash = box_draws_local_hash(it->world, e);
        ecs_map_val_t *last = ecs_map_ensure(&d->locals, e);
        if (*last == hash) {
            continue;
        }

        *last = hash;
        *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
            .root = e, .kinds = BOX_DRAW_TRANSFORM | BOX_DRAW_FADE
        };
    }

done:
    return;
}

/** Remember the entities that got or lost an owned DynamicTransform as roots of subtrees whose
 * mover changed.
 *
 * The root itself takes its key again: a shape under a mover that owns DynamicTransform leaves
 * the mover batches, and joins them again when it loses it.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnMover(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    for (i = 0; !ecs_is_fini(it->world) && i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        box_draws_mark(it->world, d, e, BOX_DRAW_ALL);
        *ecs_vec_append_t(NULL, &d->moved, box_draw_moved_t) = (box_draw_moved_t){
            .root = e, .kinds = BOX_DRAW_TRANSFORM | BOX_DRAW_FADE
        };
    }
}

void box_draws_mover_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_id_t ids[] = { ecs_id(Position), ecs_id(Rotation), ecs_id(Scale) };
    const char *names[] = { "DrawsOnLocalPosition", "DrawsOnLocalRotation", "DrawsOnLocalScale" };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_draws_observer(world, d, names[i], (ecs_observer_desc_t){
            .query.terms = {
                { .id = ids[i] },
                { .id = DynamicTransform, .inout = EcsInOutNone }
            },
            .events = { EcsOnSet } }, BoxDrawsOnLocal);
    }

    box_draws_observer(world, d, "DrawsOnManualWorld", (ecs_observer_desc_t){
        .query.terms = {
            { .id = ecs_id(WorldTransform) },
            { .id = ManualTransform, .inout = EcsInOutNone },
            { .id = DynamicTransform, .inout = EcsInOutNone }
        },
        .events = { EcsOnSet } }, BoxDrawsOnLocal);
    box_draws_observer(world, d, "DrawsOnMover", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicTransform, .src.id = EcsSelf }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMover);
}

void box_draws_live_frames(
    const ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i, s, count = ecs_vec_count(&d->batches);
    for (i = 0; i < count; i ++) {
        box_draw_batch_t *b = box_draws_batch(d, i);
        if (!b->alive || !b->key.mover || !b->key.frame || !b->live_frames) {
            continue;
        }

        for (s = 0; s < ecs_vec_count(&b->entities); s ++) {
            box_draw_frame_src_t *src = box_draws_slot_frame_src(b, s);
            box_render_transform_t tmp;
            if (src->live) {
                box_draws_frame_record(world, d, src, box_draws_slot_frame(b, s),
                    box_draws_slot_world(d, b, s, &tmp));
            }
        }
    }
}

void box_draws_composes(
    box_render_packet_t *p,
    box_render_draws_t *d)
{
    static const int32_t stores[] = {
        BOX_DRAWS_STORE_MOVER, BOX_DRAWS_STORE_MOVER_SURFACE, BOX_DRAWS_STORE_MOVER_FRAME,
        BOX_DRAWS_STORE_MOVER_FRAME_SURFACE
    };
    int32_t i, pg, composes = ecs_vec_count(&p->composes);
    for (i = 0; i < (int32_t)(sizeof(stores) / sizeof(stores[0])); i ++) {
        box_draw_store_t *store = &d->stores[stores[i]];
        for (pg = 0; pg < ecs_vec_count(&store->pages); pg ++) {
            const box_draw_page_t *page = box_draws_store_page(store, pg);
            if (!page->alive || !page->capacity || !page->top) {
                continue;
            }

            box_render_compose_t *c = box_render_packet_compose(p);
            c->page = box_draws_store_handle(store, pg, BOX_RENDER_STREAM_TRANSFORM);
            c->count = page->top < page->capacity ? page->top : page->capacity;
        }
    }

    if (ecs_vec_count(&p->composes) > composes) {
        box_draws_movers_upload(d, p);
    }
}

bool box_draws_mover_table(
    const ecs_world_t *world,
    const ecs_table_t *table,
    bool flat,
    ecs_id_t spread)
{
    bool carried = ecs_table_has_id(world, table, spread) &&
        !ecs_table_has_id(world, table, DynamicTransform);
    bool cell = ecs_id(WorldCell) && ecs_table_has_id(world, table, ecs_id(WorldCell));

    return !flat && (carried || cell) &&
        !ecs_table_has_id(world, table, TransitionTransform) &&
        !ecs_table_has_id(world, table, ecs_id(PlanarReflection));
}

void box_draws_shadow_slot(
    const ecs_world_t *world,
    box_render_draws_t *d,
    box_draw_batch_t *b,
    int32_t slot)
{
    box_render_transform_t tmp;
    if (d->shadow_record && box_draws_caster(b)) {
        box_draws_shadow_region(world, d, b, box_draws_slot_world(d, b, slot, &tmp),
            box_draws_slot_surface(b, slot));
    }
}
