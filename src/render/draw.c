/**
 * @file render/draw.c
 * @brief Tracks which shapes changed and keeps them in their instanced batch.
 *
 * Observers collect changed entities and the roots of changed subtrees, so an extract only
 * visits what changed; static instances are written once per change, dynamic ones per frame.
 */

#include "draw.h"

ECS_COMPONENT_DECLARE(DrawSlot);

/** Construct DrawSlot values that are not in a batch.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    DrawSlot *slots = ptr;
    int32_t i;
    (void)ti;
    for (i = 0; i < count; i ++) {
        slots[i] = (DrawSlot){
            .batch = -1, .slot = -1, .fade = 1.0f, .tint = { .keep = 1.0f }
        };
    }
}

/** Copy DrawSlot values: a copy is a new entity that has no slot yet.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_copy(
    void *dst,
    const void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)src;
    box_draw_slot_ctor(dst, count, ti);
}

/** Move DrawSlot values with the entity they belong to.
 *
 * @param dst The destination components
 * @param src The source components
 * @param count Number of components
 * @param ti Type info
 */
static void box_draw_slot_move(
    void *dst,
    void *src,
    int32_t count,
    const ecs_type_info_t *ti)
{
    ecs_os_memcpy(dst, src, ti->size * count);
}

/** Drop a reference to the state, freeing it with the last one.
 *
 * @param ctx The state
 */
static void box_draws_free(
    void *ctx)
{
    box_render_draws_t *d = ctx;
    if (-- d->refs) {
        goto done;
    }

    box_draws_batches_fini(d);
    ecs_map_fini(&d->meshes);
    ecs_vec_fini_t(NULL, &d->released, uint32_t);
    ecs_vec_fini_t(NULL, &d->blended, int32_t);
    ecs_vec_fini_t(NULL, &d->sorted, box_draw_sorted_t);
    ecs_vec_fini_t(NULL, &d->dirty, ecs_entity_t);
    ecs_vec_fini_t(NULL, &d->moved, ecs_entity_t);
    ecs_vec_fini_t(NULL, &d->walk, ecs_entity_t);
    ecs_os_free(d);
done:
    return;
}

void box_draws_mark(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    DrawSlot *s = ecs_get_mut(world, e, DrawSlot);
    if (s && !s->dirty) {
        s->dirty = true;
        *ecs_vec_append_t(NULL, &d->dirty, ecs_entity_t) = e;
    }
}

/** Mark the entities of an observer event as changed.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnChange(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        box_draws_mark(it->world, d, it->entities[i]);
    }

done:
    return;
}

/** Remember the entities of an observer event as roots of changed subtrees.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnMove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        *ecs_vec_append_t(NULL, &d->moved, ecs_entity_t) = it->entities[i];
    }

done:
    return;
}

/** Free the slots of entities that lose their shape.
 *
 * @param it The iterator of the observer
 */
static void BoxDrawsOnRemove(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    int32_t i;
    if (ecs_is_fini(it->world)) {
        goto done;
    }

    for (i = 0; i < it->count; i ++) {
        DrawSlot *s = ecs_get_mut(it->world, it->entities[i], DrawSlot);
        if (s && s->batch >= 0) {
            box_draws_slot_remove(it->world, d, s->batch, s->slot);
            s->batch = -1;
            s->slot = -1;
        }
    }

done:
    return;
}

/** Mark every entity of the changed subtrees, the roots included.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_walk(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i;
    for (i = 0; i < ecs_vec_count(&d->moved); i ++) {
        ecs_entity_t root = *ecs_vec_get_t(&d->moved, ecs_entity_t, i);
        if (!ecs_is_alive(world, root)) {
            continue;
        }

        *ecs_vec_append_t(NULL, &d->walk, ecs_entity_t) = root;
        while (ecs_vec_count(&d->walk)) {
            ecs_entity_t e = *ecs_vec_last_t(&d->walk, ecs_entity_t);
            ecs_vec_remove_last(&d->walk);
            box_draws_mark(world, d, e);
            ecs_iter_t it = ecs_children(world, e);
            while (ecs_children_next(&it)) {
                int32_t c;
                for (c = 0; c < it.count; c ++) {
                    *ecs_vec_append_t(NULL, &d->walk, ecs_entity_t) = it.entities[c];
                }
            }
        }
    }

    ecs_vec_clear(&d->moved);
}

float box_draws_fade(
    const ecs_world_t *world,
    ecs_entity_t e,
    box_draw_tint_t *tint)
{
    float result = 1.0f;
    ecs_entity_t cur = ecs_get_parent(world, e);
    *tint = (box_draw_tint_t){ .keep = 1.0f };
    while (cur) {
        const Opacity *opacity = ecs_get(world, cur, Opacity);
        const Tint *t = ecs_get(world, cur, Tint);
        if (opacity) {
            result *= glm_clamp(opacity->value, 0.0f, 1.0f);
        }

        if (t) {
            float a = (float)t->a / 255.0f;
            float weight = a * tint->keep / 255.0f;
            tint->add[0] += (float)t->r * weight;
            tint->add[1] += (float)t->g * weight;
            tint->add[2] += (float)t->b * weight;
            tint->keep *= 1.0f - a;
        }

        cur = ecs_get_parent(world, cur);
    }

    return result;
}

/** Compute the batch key of a shape.
 *
 * @param world The world
 * @param e The entity
 * @param shape Its mesh
 * @param alpha Resolved alpha of the shape
 * @param key Receives the key
 */
static void box_draws_key(
    ecs_world_t *world,
    ecs_entity_t e,
    const ShapeMesh *shape,
    float alpha,
    box_draw_key_t *key)
{
    const BackFaceMode *back_face = ecs_get(world, e, BackFaceMode);
    const PbrTextures *textures = ecs_get(world, e, PbrTextures);
    const TextureWrap *wrap = ecs_get(world, e, TextureWrap);
    ecs_os_memset_t(key, 0, box_draw_key_t);
    key->blend = box_draws_blend(ecs_get(world, e, BlendMode), alpha);
    key->back_face = back_face ? (uint8_t)*back_face : (uint8_t)BackFaceCull;
    key->wrap = wrap ? (uint8_t)*wrap : (uint8_t)WrapRepeat;
    if (textures) {
        key->textures[0] = textures->albedo;
        key->textures[1] = textures->emissive;
        key->textures[2] = textures->roughness;
        key->textures[3] = textures->normal;
    }

    key->scene = box_render_scene_of(world, e);
    key->mesh = shape->mesh;
    key->pipeline = BOX_RENDER_SHADER_MESH;
    key->dynamic = ecs_has_id(world, e, DynamicTransform) ||
        ecs_has_id(world, e, DynamicMaterial) ||
        ecs_get_target_for_id(world, e, EcsChildOf, DynamicMaterial) != 0;
    key->shadow = !ecs_has_id(world, e, NoShadow) &&
        !ecs_get_target_for_id(world, e, EcsChildOf, NoShadow);
}

/** Bring a changed entity into the batch of its key and write its static instance.
 *
 * @param world The world
 * @param d The state
 * @param e The entity
 */
static void box_draws_update(
    ecs_world_t *world,
    box_render_draws_t *d,
    ecs_entity_t e)
{
    box_draw_key_t key;
    box_draw_batch_t *b;
    box_draw_material_t material;
    DrawSlot *s = ecs_is_alive(world, e) ? ecs_get_mut(world, e, DrawSlot) : NULL;
    if (!s) {
        goto done;
    }

    const ShapeMesh *shape = ecs_get(world, e, ShapeMesh);
    bool drawn = shape && shape->mesh && !ecs_has_id(world, e, EcsPrefab);
    s->dirty = false;
    d->stats.processed ++;
    if (drawn) {
        s->fade = box_draws_fade(world, e, &s->tint);
        material = (box_draw_material_t){
            .color = ecs_get(world, e, Rgb),
            .opacity = ecs_get(world, e, Opacity),
            .pbr = ecs_get(world, e, PbrMaterial),
            .emissive = ecs_get(world, e, Emissive),
            .uv = ecs_get(world, e, TextureTransform),
            .tint = ecs_get(world, e, Tint),
            .inherited = &s->tint,
            .fade = s->fade
        };
        box_draws_key(world, e, shape, box_draws_alpha(&material), &key);
    }

    if (s->batch >= 0) {
        b = box_draws_batch(d, s->batch);
        if (drawn && !ecs_os_memcmp_t(&b->key, &key, box_draw_key_t)) {
            goto write;
        }

        box_draws_slot_remove(world, d, s->batch, s->slot);
        s->batch = -1;
        s->slot = -1;
    }

    if (!drawn) {
        goto done;
    }

    s->batch = box_draws_batch_ensure(d, &key);
    s->slot = box_draws_slot_add(d, s->batch, e);
    b = box_draws_batch(d, s->batch);
write:
    box_draws_instance(ecs_vec_get_t(&b->instances, box_render_instance_t, s->slot),
        ecs_get(world, e, WorldTransform), shape, &material);
    box_draws_slot_changed(b, s->slot);
done:
    return;
}

/** Bring the changed entities into their batches.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_process(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    int32_t i;
    box_draws_walk(world, d);
    for (i = 0; i < ecs_vec_count(&d->dirty); i ++) {
        box_draws_update(world, d, *ecs_vec_get_t(&d->dirty, ecs_entity_t, i));
    }

    ecs_vec_clear(&d->dirty);
}

/** Update the batches with what changed and send what the views of the frame draw.
 *
 * @param it The iterator of the system
 */
static void BoxRenderExtractDraws(
    ecs_iter_t *it)
{
    box_render_draws_t *d = it->ctx;
    ecs_world_t *world = it->world;
    box_mesh_cache_t *cache = box_mesh_cache_of(world);
    box_render_packet_t *p = box_render_packet(world);
    const box_render_backend_t *backend = box_render_get_backend(world);
    if (!cache) {
        goto done;
    }

    if (backend->ctx != d->backend || backend->render != d->render) {
        box_draws_reset_backend(d, cache);
        d->backend = backend->ctx;
        d->render = backend->render;
    }

    d->stats.processed = 0;
    d->stats.scattered = 0;
    d->stats.upload_bytes = 0;
    box_draws_process(world, d);
    box_draws_send(world, d, p);
    RenderStatus *status = ecs_singleton_get_mut(world, RenderStatus);
    if (status) {
        status->upload_bytes = d->stats.upload_bytes;
    }

done:
    return;
}

/** Register an observer of the instanced batches.
 *
 * @param world The world
 * @param d The state
 * @param name Name of the observer
 * @param desc Terms and events of the observer
 * @param callback What the observer does
 */
static void box_draws_observer(
    ecs_world_t *world,
    box_render_draws_t *d,
    const char *name,
    ecs_observer_desc_t desc,
    ecs_iter_action_t callback)
{
    desc.entity = ecs_entity(world, { .name = name });
    desc.callback = callback;
    desc.ctx = d;
    desc.ctx_free = box_draws_free;
    d->refs ++;
    ecs_observer_init(world, &desc);
}

/** Register the observers of the material components.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_material_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_entity_t components[6] = {
        ecs_id(PbrMaterial), ecs_id(Emissive), ecs_id(BlendMode), ecs_id(BackFaceMode),
        ecs_id(TextureWrap), ecs_id(TextureTransform)
    };
    const char *names[6] = {
        "DrawsOnPbrMaterial", "DrawsOnEmissive", "DrawsOnBlendMode", "DrawsOnBackFaceMode",
        "DrawsOnTextureWrap", "DrawsOnTextureTransform"
    };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        box_draws_observer(world, d, names[i], (ecs_observer_desc_t){
            .query.terms = {{ .id = components[i] }},
            .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnChange);
    }

    box_draws_observer(world, d, "DrawsOnPbrTextures", (ecs_observer_desc_t){
        .query.terms = {{
            .id = ecs_id(PbrTextures),
            .src.id = EcsSelf | EcsUp,
            .trav = EcsIsA
        }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnChange);

    box_draws_observer(world, d, "DrawsOnOpacity", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Opacity) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnTint", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Tint) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnMove);
}

/** Register the observers that collect changed entities and changed subtrees.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    box_draws_observer(world, d, "DrawsOnShape", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnSet } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnShapeRemove", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(ShapeMesh) }},
        .events = { EcsOnRemove } }, BoxDrawsOnRemove);
    box_draws_observer(world, d, "DrawsOnColor", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Rgb) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_material_observers(world, d);
    box_draws_observer(world, d, "DrawsOnDynamic", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicTransform }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnWorldTransform", (ecs_observer_desc_t){
        .query.terms = {
            { .id = ecs_id(WorldTransform) },
            { .id = DynamicTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet } }, BoxDrawsOnChange);
    box_draws_observer(world, d, "DrawsOnDynamicMaterial", (ecs_observer_desc_t){
        .query.terms = {{ .id = DynamicMaterial }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnNoShadow", (ecs_observer_desc_t){
        .query.terms = {{ .id = NoShadow }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnScene", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(Scene) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnChildOf", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .events = { EcsOnAdd, EcsOnRemove } }, BoxDrawsOnMove);
    box_draws_observer(world, d, "DrawsOnParent", (ecs_observer_desc_t){
        .query.terms = {{ .id = ecs_id(EcsParent) }},
        .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnMove);
}

/** Register the observers of the components a static transform is computed from.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_transform_observers(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    ecs_entity_t components[3] = { ecs_id(Position), ecs_id(Rotation), ecs_id(Scale) };
    const char *names[3] = { "DrawsOnPosition", "DrawsOnRotation", "DrawsOnScale" };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_draws_observer(world, d, names[i], (ecs_observer_desc_t){
            .query.terms = {
                { .id = components[i] },
                { .id = DynamicTransform, .oper = EcsNot },
                { .id = ManualTransform, .oper = EcsNot }
            },
            .events = { EcsOnSet, EcsOnRemove } }, BoxDrawsOnMove);
    }
}

/** Create the queries of the dynamic shapes.
 *
 * @param world The world
 * @param d The state
 */
static void box_draws_queries(
    ecs_world_t *world,
    box_render_draws_t *d)
{
    d->dynamic_transform = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Opacity), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PbrMaterial), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Emissive), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(BlendMode), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(TextureTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = DynamicTransform, .inout = EcsInOutNone },
            {
                .id = DynamicMaterial,
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone,
                .oper = EcsOptional
            },
            { .id = ecs_id(Tint), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    d->dynamic_material = ecs_query(world, {
        .terms = {
            { .id = ecs_id(ShapeMesh), .inout = EcsIn },
            { .id = ecs_id(DrawSlot), .inout = EcsIn },
            { .id = ecs_id(WorldTransform), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Rgb), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Opacity), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(PbrMaterial), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(Emissive), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(BlendMode), .inout = EcsIn, .oper = EcsOptional },
            { .id = ecs_id(TextureTransform), .inout = EcsIn, .oper = EcsOptional },
            {
                .id = DynamicMaterial,
                .src.id = EcsSelf | EcsUp,
                .trav = EcsChildOf,
                .inout = EcsInOutNone
            },
            { .id = DynamicTransform, .inout = EcsInOutNone, .oper = EcsNot },
            { .id = ecs_id(Tint), .inout = EcsIn, .oper = EcsOptional }
        },
        .cache_kind = EcsQueryCacheAuto
    });
}

void box_render_draws_init(
    ecs_world_t *world)
{
    box_render_draws_t *d = ecs_os_calloc_t(box_render_draws_t);
    box_draws_tables_init();
    ECS_COMPONENT_DEFINE(world, DrawSlot);
    ecs_set_hooks(world, DrawSlot, {
        .ctor = box_draw_slot_ctor,
        .copy = box_draw_slot_copy,
        .move = box_draw_slot_move
    });
    ecs_add_pair(world, ecs_id(ShapeMesh), EcsWith, ecs_id(DrawSlot));

    ecs_vec_init_t(NULL, &d->batches, box_draw_batch_t, 0);
    ecs_vec_init_t(NULL, &d->free_batches, int32_t, 0);
    ecs_vec_init_t(NULL, &d->free_buffers, uint32_t, 0);
    ecs_vec_init_t(NULL, &d->dirty, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->moved, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->walk, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &d->released, uint32_t, 0);
    ecs_vec_init_t(NULL, &d->blended, int32_t, 0);
    ecs_vec_init_t(NULL, &d->sorted, box_draw_sorted_t, 0);
    ecs_map_init(&d->lookup, NULL);
    ecs_map_init(&d->meshes, NULL);
    d->next_buffer = BOX_RENDER_BUFFER_FIRST;
    d->refs = 1;
    box_draws_queries(world, d);
    box_draws_observers(world, d);
    box_draws_transform_observers(world, d);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "RenderExtractDraws" }),
        .phase = RenderExtract,
        .run = BoxRenderExtractDraws,
        .ctx = d,
        .ctx_free = box_draws_free
    });
}

void box_render_draws_stats(
    const ecs_world_t *world,
    box_render_draws_stats_t *stats)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.RenderExtractDraws");
    const ecs_system_t *s = system ? ecs_system_get(world, system) : NULL;
    ecs_os_memset_t(stats, 0, box_render_draws_stats_t);
    if (s && s->ctx) {
        *stats = ((const box_render_draws_t*)s->ctx)->stats;
    }
}
